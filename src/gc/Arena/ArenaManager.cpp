// Hack
#include "../common.h"
#include "arenaManager.h"
#include "ArenaAllocator.h"
#include "ArenaHeader.h"
#include "vector.h"
#include "../tls.h"
#include "../object.h"

#define FEATURE_IMPLICIT_TLS

// thread_local data. 
// TlsIdx_ArenaStack,
//
//#include "threads.h"
//static const int v = offsetof(Thread, m_arenaStack);
//static_assert(v == 1896, "v is not 1896");

size_t ArenaManager::virtualBase;
TypeHandle LoadExactFieldType(FieldDesc *pFD, MethodTable *pEnclosingMT, AppDomain *pDomain);

// lastId keeps track of the last id used to create an arena.  
// the next one allocated will try at lastId+1, and keep trying
// until an empty one is found.  this means that ids will not be
// reused immediately, which means that a buffer becomes unaccessable
// for as long as possible after it is free.

unsigned int ArenaManager::lastId = 0;
void* ArenaManager::arenaById[maxArenas];
unsigned long ArenaManager::refCount[maxArenas];

void ArenaManager::InitArena()
{
	for (int i = 0; i < maxArenas; i++)
	{
		arenaById[i] = nullptr;
		refCount[i] = 0;
	}

	lastId = 0;
	virtualBase = (size_t)ClrVirtualAlloc((LPVOID)arenaBaseRequest, maxArenas * (1ULL << arenaAddressShift), MEM_RESERVE, PAGE_NOACCESS);
	if (virtualBase != arenaBaseRequest)
	{
		throw "could not reserve virtual address space for arena buffers";
	}
}

void ArenaManager::DereferenceId(int id)
{
	assert(arenaById[id]);
	unsigned long& r = refCount[id];
	assert(r > 0);
	if (0 == InterlockedDecrement(&r))
	{
		DeleteAllocator(arenaById[id]);
		arenaById[id] = nullptr;
	}
}

void ArenaManager::ReferenceId(int id)
{
	assert(arenaById[id]);
	unsigned long& r = refCount[id];
	assert(r > 0);
	InterlockedIncrement(&r);
}

int ArenaManager::getId()
{
	int id = lastId + 1;
	int cnt = 0;
	for (int id = lastId + 1; cnt < maxArenas; id = (id + 1) % maxArenas, cnt++)
	{
		if (!refCount[id])
		{
			auto was = lastId;
			if (was == InterlockedCompareExchange(&lastId, id, was))
			{
				refCount[id] = true;
				return id;
			}
		}
	}
	throw "insufficient arena allocators";
}

size_t ArenaManager::IdToAddress(int id)
{
	return ((size_t)id << arenaAddressShift) + virtualBase;
}

int _cdecl ArenaManager::GetArenaId()
{
	Arena* arena = (Arena*)GetArenaStack().Current();
	if (arena == nullptr) return -1;
	return Id(arena);
}

// Hack
// 1 = reset to GCHeap
// 2 = push new arena allocator
// 3 = push GCHeap
// 4 = pop
// 1024+ push old arena
void _cdecl ArenaManager::SetAllocator(unsigned int type)
{
	ArenaStack& arenaStack = GetArenaStack();
	Arena* current_arena;
	switch (type)
	{
	case 1:
		for (int i = 0; i < arenaStack.Size(); i++)
		{
			DereferenceId(Id(arenaStack[i]));
		}

		arenaStack.Reset();
		break;
	case 2:
		current_arena = MakeArena();
		{
			DWORD written;
			char bufn[25];
			_itoa((size_t)current_arena>>32, bufn, 16);

			WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bufn, (DWORD)strlen(bufn), &written, 0);
			WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Arena\n", (DWORD)strlen("Arena\n"), &written, 0);

		}
		arenaStack.Push(current_arena);
		//Log("Arena Push", arenaStack.Size());
		if (GetArenaStack().Size() > 3)
		{
			//Log("over Arena Push", GetArenaStack().Size());
		}
		break;
	case 3:
		PushGC();
		break;
	case 4:
		Pop();
		break;
	default:
		assert(type >= 1024 && type < (1024 + maxArenas));
		ReferenceId(type - 1024);
		current_arena = (Arena*)arenaById[type - 1024];
		assert(current_arena != nullptr);
		arenaStack.Push(current_arena);
		//Log("Arena reuse Push", arenaStack.Size());
		if (GetArenaStack().Size() > 1)
		{
			//Log("over Arena reuse Push", GetArenaStack().Size());
		}
	}
}

ArenaHeader& GetHeaderFromAddress(void* address)
{
	size_t a = (size_t)address;
	a &= ~((1ULL << ArenaManager::arenaAddressShift) - 1);
	return *(ArenaHeader*)a;
}


Arena* ArenaManager::MakeArena()
{
	unsigned int id = getId();
	Arena stackArena(Arena::Config(minBufferSize, maxBufferSize, IdToAddress(id)));
	ArenaHeader* header = (ArenaHeader*)stackArena.Allocate(sizeof(ArenaHeader));
	new (header)ArenaHeader(std::move(stackArena));
	header->m_arena.PropagateAllocators();
	Arena*arena = &(header->m_arena);
	arenaById[id] = arena;

	//header->clones[0] = ObjectHashTable::Create(*arena);
	//Object* a = (Object*)0x100012345678;
	//Object*b = (Object*)0x100087654321;
	//(*header->clones[0])[a] = b;

	//for (int i = 0; i < 1000; i++)
	//{
	//	Object* a = (Object*)(i * 48);
	//	Object* b = (Object*)(i * 99);
	//	(*header->clones[0])[a] = b;
	//	auto x = (*header->clones[0])[a];
	//	assert(b == x);
	//}
	//for (int i = 0; i < 1000; i++)
	//{
	//	Object* a = (Object*)(i * 48);
	//	Object* b = (Object*)(i * 99);
	//	auto x = (*header->clones[0])[a];
	//	assert(b == x);
	//}
	return arena;
}

void _cdecl ArenaManager::PushGC()
{
	GetArenaStack().Push(nullptr);
	//Log("GC Push", GetArenaStack().Size());
	if (GetArenaStack().Size() > 3)
	{
		//Log("over GC Push", GetArenaStack().Size());
	}
}

void _cdecl ArenaManager::Pop()
{
	void* allocator = GetArenaStack().Pop();
	if (allocator != nullptr)
	{
		DereferenceId(Id(allocator));
		//Log("Arena Pop", GetArenaStack().Size());
		if (GetArenaStack().Size() > 3)
		{
			//Log("over GC Pop", GetArenaStack().Size());
		}
	}
	else {
		//Log("GC Pop", GetArenaStack().Size());
	}
}

ObjectHashTable& ArenaManager::FindCloneTable(void* src, void* target)
{
	// There needs to be a hashtable that keeps track of every clone that occurs between each pair of allocators.
	// the hash table is always stored in the arena with the highest address.  So each hashtable contains both the
	// clones from A to B and also the clones from B to A.
	// one of the two allocators may be the GC allocator, but these objects will always have the lowest address, so the
	// hash table will still be built into an arena allocators header.
	// each header contains an array with entries for the maximum number of allowed arenas. 
	// example:  If a clone from arena 3 was made in arena 8, then slot #3 in in the header for arena 8 contains all
	// clones made from 8 to 3 and from 3 to 8.
	// Whenever a clone is made from (or to) the GC for example to arena 22, there is no slot number for the GC, so
	// slot 22 in arena 22 is reserved for cloning between 22 and the GC.
	//
	// If an allocator 17 is deleted, all of his clone hash tables will be deleted.  We also need to go through all of the other
	// arena headers, and delete the table in slot 17 for each one.  (deleting an object in an arena simply means setting the pointer
	// to zero.)
	//
	// Clone tables are necessary to make sure the same object is not cloned repeatedly, and that linked objects will have the links go
	// to a single clone, rather than create bugs by having different links go to different clones.

	// master is the allocator that will hold the clone table for this pair of addresss.  
	// The target address can be any address in the range covered by the allocator when checking for a clone,
	// but once found in the clone table, the target address is the address of the clone for the target allocator.
	void *master = (src < target) ? target : src;
	void *slave = (src > target) ? target : src;
	if (slave < (void*)arenaBaseRequest)
	{
		slave = master;
	}
	int id = Id(slave);


	auto header = GetHeaderFromAddress(master);
	if (header.clones[id] == nullptr)
	{
		header.clones[id] = ObjectHashTable::Create(header.m_arena);
	}
	return *header.clones[id];
}

Object* ArenaManager::FindClone(void* src, void* target)
{
	auto cloneTable = FindCloneTable(src, target);
	bool found;
	Object* clone = cloneTable.Find((Object*)src, found);
	if (found)
	{
		return clone;
	}
	else
	{
		return nullptr;
	}
}

bool ArenaManager::SetClone(void* src, void* target)
{
	auto cloneTable = FindCloneTable(src, target);
	return cloneTable.Set((Object*)src, (Object*)target);
}

void* ArenaManager::GetArena()
{
	return GetArenaStack().Current();
}

unsigned int ArenaManager::Id(void * allocator)
{
	return (((Arena*)allocator)->m_config.addr >> arenaAddressShift) & (maxArenas - 1);
}

void ArenaManager::DeleteAllocator(void* vallocator)
{
	if (vallocator == nullptr) return;

	int id = Id(vallocator);
	Arena* allocator = static_cast<Arena*> (vallocator);

	// dispose of clone cache items for disposed arena
	for (int i = 0; i < maxArenas; i++)
	{
		Arena* a = (Arena*)arenaById[i];
		if (a != nullptr)
		{
			ArenaHeader* h = (ArenaHeader*)a;
			h->clones[id] = nullptr;
		}
	}
}

inline
size_t ArenaManager::Align(size_t nbytes, int alignment)
{
	return (nbytes + alignment) & ~alignment;
}

inline void* ArenaManager::AllocatorFromAddress(void * addr)
{
	size_t iaddr = (size_t)addr;
	if (iaddr < arenaBaseRequest) return nullptr;
	unsigned int id = (iaddr >> arenaAddressShift) & (maxArenas - 1);
	return arenaById[id];
}

int cnty = 0;
int cntx = 0;
void* ArenaManager::Allocate(size_t jsize)
{
	Arena* arena = (Arena*)GetArenaStack().Current();
	if (arena == nullptr)
	{
		return nullptr;
	}

	size_t size = Align(jsize) + headerSize;
	void*ret = (arena)->Allocate(size);
	
//	memset(ret, 0, size); it already is zero
	return (void*)((char*)ret + headerSize);
}

void* ArenaManager::Allocate(Arena* arena, size_t jsize)
{
	size_t size = Align(jsize) + headerSize;
	void*ret = (arena)->Allocate(size);
	memset(ret, 0, size);
	return (void*)((char*)ret + headerSize);
}

int lcnt = 0;
volatile DWORD spinlock;
int lockdep = 0;
void ArenaManager::Log(char *str, size_t n, size_t n2, char*hdr)
{
	return;
	int spinCnt = 0;
	auto tid = GetCurrentThreadId();

	for (;;)
	{
		auto was = ::InterlockedCompareExchange(&spinlock, tid, 0);
		if (was == 0)
		{
			lockdep = 0;
			break;
		}
		if (was == tid)
		{
			lockdep++;
			break;
		}
	}
	
	auto& arenaStack = GetArenaStack();
	
	
	
	DWORD written;
	char bufn[25];
	_itoa(lcnt++, bufn, 10);

	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bufn, (DWORD)strlen(bufn), &written, 0);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "[", (DWORD)strlen("["), &written, 0);
	_itoa(tid, bufn, 10);

	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bufn, (DWORD)strlen(bufn), &written, 0);
	_itoa(arenaStack.Size(), bufn, 10);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "@", (DWORD)strlen("@"), &written, 0);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bufn, (DWORD)strlen(bufn), &written, 0);
	if (arenaStack.Current()!=nullptr)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "#", (DWORD)strlen("#"), &written, 0);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "] Log ", (DWORD)strlen("] Log "), &written, 0);
	if (hdr != nullptr)
	{
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), hdr, (DWORD)strlen(hdr), &written, 0);
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), " = ", (DWORD)strlen(" = "), &written, 0);
	}

	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), str, (DWORD)strlen(str), &written, 0);
	if (n != 0)
	{
		char buf[25];
		buf[0] = ':'; buf[1] = ' ';
		buf[2] = '0'; buf[3] = 'x';
		_ui64toa(n, buf + 4, 16);
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)strlen(buf), &written, 0);
	}
	if (n2 != 0)
	{
		char buf[25];
		buf[0] = ' '; buf[1] = '-';
		buf[2] = '>'; buf[3] = ' ';
		buf[4] = '0'; buf[5] = 'x';
		_ui64toa(n2, buf + 6, 16);
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)strlen(buf), &written, 0);
	}
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\n", (DWORD)strlen("\n"), &written, 0);
	if (lockdep-- <= 0)
	{
		spinlock = 0;
	}
}

alloc_context* GetThreadAllocContext();

volatile DWORD spinlock2 = 0;
int lockdepth = 0;

void*  ArenaManager::ArenaMarshall(void*vdst, void*vsrc)
{
	if (vsrc == nullptr) return nullptr;
			auto tid = GetCurrentThreadId();
		for (;;)
		{
			auto was = ::InterlockedCompareExchange(&spinlock2, tid, 0);
			if (was == 0)
			{
				lockdepth = 0;
				break;
			}
			if (was == tid)
			{
				lockdepth++;
				break;
			}
		}
		cntx++;
		
		if (cntx % 100 == 0 || cntx>9100)
		{
			DWORD written;
			char bufn[25];
			_itoa(cntx, bufn, 10);

			WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bufn, (DWORD)strlen(bufn), &written, 0);
			WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "M\n", (DWORD)strlen("M\n"), &written, 0);

		}


	//Log("ArenaMarshall", (size_t)vsrc, (size_t)vdst);

	// src Allocator is not used
	Arena* srcAllocator = (Arena*)AllocatorFromAddress(vsrc);
	Arena* dstAllocator = (Arena*)AllocatorFromAddress(vdst);

	Object* src = (Object*)vsrc;
	size_t size =  src->GetSize();

	void *p = nullptr;
	bool bContainsPointers = false;
	bool bFinalize = false;
	if (dstAllocator == nullptr)
	{
		PushGC();
		DWORD flags = ((bContainsPointers ? GC_ALLOC_CONTAINS_REF : 0) |
			(bFinalize ? GC_ALLOC_FINALIZE : 0));
		if (GCHeap::UseAllocationContexts())
			p = GCHeap::GetGCHeap()->Alloc(GetThreadAllocContext(), size, flags);
		else
			p = GCHeap::GetGCHeap()->Alloc(size, flags);
		Pop();
	}
	else
	{
		p = Allocate(dstAllocator, size);
	}

	//Log("Cloned object allocated ", (size_t)p, size);
	// We know this is not a ByValueClass, but we must do our own reflection, so we start with copying the whole
	// class, and we will fix the fields that are not value v
	for (size_t*ip = (size_t*)vsrc, *op = (size_t*)p; ip < (size_t*)((char*)vsrc + size); ) *op++ = *ip++;
	//Log("memwrite", (size_t)p, (size_t)p + size);
	PTR_MethodTable pMT = src->GetMethodTable();
	if (pMT->IsArray())
	{
		CloneArray(p, src, pMT, sizeof(ArrayBase),size);
	}
	else
	{
		CloneClass(p, src, pMT, sizeof(Object));
	}
	//Log("ArenaMarshall End");
	if (lockdepth--<=0)
	spinlock2 = 0;
	return p;
}

void ArenaManager::CloneArray(void* dst, Object* src, PTR_MethodTable pMT, int ioffset,size_t size)
{
#if DEBUG
	//Log((char*)pMT->GetDebugClassName(), 0, 0, "clonearray");
#endif
	TypeHandle arrayTypeHandle = ArrayBase::GetTypeHandle(pMT);
	ArrayTypeDesc* ar = arrayTypeHandle.AsArray();
	TypeHandle ty = ar->GetArrayElementTypeHandle();
	size_t componentSize = src->GetSize();
	ArrayBase* refArray = (ArrayBase*)src;
	DWORD numComponents = refArray->GetNumComponents();
	const CorElementType arrayElType = ty.GetVerifierCorElementType();

	switch (arrayElType) {

	case ELEMENT_TYPE_I1:
	case ELEMENT_TYPE_U1:
	case ELEMENT_TYPE_BOOLEAN:
	case ELEMENT_TYPE_I2:
	case ELEMENT_TYPE_U2:
	case ELEMENT_TYPE_CHAR:
	case ELEMENT_TYPE_I4:
	case ELEMENT_TYPE_U4:
	case ELEMENT_TYPE_R4:
	IN_WIN32(case ELEMENT_TYPE_I:)
	IN_WIN32(case ELEMENT_TYPE_U:)
	case ELEMENT_TYPE_I8:
	case ELEMENT_TYPE_U8:
	case ELEMENT_TYPE_R8:
	IN_WIN64(case ELEMENT_TYPE_I:)
	IN_WIN64(case ELEMENT_TYPE_U:)
		break;
	case ELEMENT_TYPE_FNPTR:
	case ELEMENT_TYPE_PTR:
		break;
	case ELEMENT_TYPE_VALUETYPE:
		for (size_t i = ioffset; i < ioffset + numComponents*componentSize; i += componentSize)
		{
			CloneClass((char*)dst+i, (Object*)((char*)src +i), ty.AsMethodTable(), 0);
		}

		break;
	case ELEMENT_TYPE_STRING:
		break;
	case ELEMENT_TYPE_CLASS: // objectrefs
	{
		for (int i = ioffset; i < ioffset+numComponents*sizeof(void*); i+= sizeof(void*))
		{
			OBJECTREF *pSrc = *(OBJECTREF **)((char*)src + i);
			if (pSrc != nullptr && (size_t)pSrc<arenaRangeEnd)
			{
				void* clone = ArenaMarshall(dst, pSrc);
				void** rdst = (void**)((char*)dst + i);
				*rdst = clone;
				//Log("memwriteptr", (size_t)rdst, (size_t)clone);
			}
		}
	}
			
		break;
	case ELEMENT_TYPE_OBJECT:
		break;
	case ELEMENT_TYPE_SZARRAY:      // single dim, zero
		break;
	case ELEMENT_TYPE_ARRAY:        // all other arrays
									// this is where we recursively follow
		break;
	default:
		_ASSERTE(!"Unrecognized primitive type in ArrayHelper::TrySZIndexOf");
	}
} 

void ArenaManager::CloneClass(void* dst, Object* src, PTR_MethodTable mt, int ioffset)
{
#if DEBUG
	//Log((char*)mt->GetDebugClassName(),0,0,"cloneclass");
#endif
	DWORD numInstanceFields = mt->GetNumInstanceFields();
	if (numInstanceFields == 0) return;
	FieldDesc *pSrcFields = mt->GetApproxFieldDescListRaw();
	if (pSrcFields == nullptr) return;
	for (DWORD i = 0; i < numInstanceFields; i++)
	{
		FieldDesc f = pSrcFields[i];
		if (f.IsStatic()) continue;
		CorElementType type = f.GetFieldType();
		DWORD offset = f.GetOffset() + ioffset;

#ifdef DEBUG
		LPCUTF8 szFieldName = f.GetDebugName();
		//Log((char*)szFieldName,offset,0,"field");
#endif


		switch (type) {
		case ELEMENT_TYPE_BOOLEAN:
		case ELEMENT_TYPE_I1:
		case ELEMENT_TYPE_U1:
		case ELEMENT_TYPE_I2:
		case ELEMENT_TYPE_U2:
		case ELEMENT_TYPE_CHAR:
		case ELEMENT_TYPE_I4:
		case ELEMENT_TYPE_U4:
		case ELEMENT_TYPE_I8:
		case ELEMENT_TYPE_U8:
		case ELEMENT_TYPE_I:
		case ELEMENT_TYPE_U:
		case ELEMENT_TYPE_R4:
		case ELEMENT_TYPE_R8:
			// basic types are handled with memcpy
			break;
		case ELEMENT_TYPE_VALUETYPE:
		{
			
			TypeHandle th = LoadExactFieldType(&pSrcFields[i], mt, GetAppDomain());
			CloneClass((char*)dst + offset, (Object*)((char*)src + offset), th.AsMethodTable(), 0);
		}
		break;
		case ELEMENT_TYPE_PTR:
		case ELEMENT_TYPE_FNPTR:
			// pointers may be dangerous, but they are
			// copied without chang
			break;
		case ELEMENT_TYPE_STRING:
		case ELEMENT_TYPE_CLASS: // objectrefs
		case ELEMENT_TYPE_OBJECT:
		case ELEMENT_TYPE_SZARRAY:      // single dim, zero
		case ELEMENT_TYPE_ARRAY:        // all other arrays
			{
				void *pSrc = *(void **)((char*)src + offset);
				if (pSrc != nullptr)
				{
#if DEBUG
					//Log((char*)((Object*)pSrc)->GetMethodTable()->GetDebugClassName(), 0, 0, "field type (to clone)");
#endif
					void** rdst = (void**)((char*)dst + offset);
					void* clone = ArenaMarshall((void*)rdst, pSrc);
					*rdst = clone;
					//Log("memwriteptr", (size_t)rdst, (size_t)clone);
				}
			}
			break;
		default:
			//Log("default", (size_t)type);
			break;
		}
	}
}

// Given a FieldDesc which may be representative and an object which contains said field, return the actual type of the field. This
// works even when called from a different appdomain from which the type was loaded (though naturally it is the caller's
// responsbility to ensure such an appdomain cannot be unloaded during the processing of this method).
TypeHandle LoadExactFieldType(FieldDesc *pFD, MethodTable *pEnclosingMT, AppDomain *pDomain)
{
	CONTRACTL{
		THROWS;
	GC_TRIGGERS;
	MODE_COOPERATIVE;
	} CONTRACTL_END;

	// Set up a field signature with the owning type providing a type context for any type variables.
	MetaSig sig(pFD, TypeHandle(pEnclosingMT));
	sig.NextArg();

	// If the enclosing type is resident to this domain or domain neutral and loaded in this domain then we can simply go get it.
	// The logic is trickier (and more expensive to calculate) for generic types, so skip the optimization there.
	if (pEnclosingMT->GetDomain() == GetAppDomain() ||
		(pEnclosingMT->IsDomainNeutral() &&
			!pEnclosingMT->HasInstantiation() &&
			pEnclosingMT->GetAssembly()->FindDomainAssembly(GetAppDomain())))
		return sig.GetLastTypeHandleThrowing();

	TypeHandle retTH;

	// Otherwise we have to do this the expensive way -- switch to the home domain for the type lookup.
	ENTER_DOMAIN_PTR(pDomain, ADV_RUNNINGIN);
	retTH = sig.GetLastTypeHandleThrowing();
	END_DOMAIN_TRANSITION;

	return retTH;
}

void* RunAllocator(void* allocator, size_t len)
{
	return ((Arena*)allocator)->Allocate(len);
}