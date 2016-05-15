// Hack
#include "../common.h"
#include "arenaManager.h"
#include "Arena.h"

#include "../object.h"
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include "class.h"

#define LOGGING
namespace WKS
{
	extern int hcnt;
}

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
volatile __int64 ArenaManager::totalMemory = 0;
HANDLE ArenaManager::hFile = 0;

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


	if (arenaById[id] == nullptr)
	{
		Log("*arenaError", id);
		assert(arenaById[id]);
	}
	unsigned long& r = refCount[id];
	assert(r > 0);
	if (0 == InterlockedDecrement(&r))
	{
		//Log("Arena is deleted", id, (size_t)arenaById[id]);
		auto arena = arenaById[id];
		arenaById[id] = nullptr;
		DeleteAllocator(arena);

	}
}

void ArenaManager::ReferenceId(int id)
{
	if (arenaById[id] == nullptr)
	{
		Log("*arenaError", id);
		assert(arenaById[id]);
	}
	unsigned long& r = refCount[id];
	if (r <= 0)
	{
		Log("*refcount error");
		assert(r > 0);
	}
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
				refCount[id] = 1;
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
	ArenaThread* current_arena;
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
		current_arena = MakeArena()->BaseArenaThread();
		//{
		//	DWORD written;
		//	char bufn[25];
		//	_itoa((size_t)current_arena>>32, bufn, 16);

		//	WriteFile(hFile, bufn, (DWORD)strlen(bufn), &written, 0);
		//	WriteFile(hFile, "Arena\n", (DWORD)strlen("Arena\n"), &written, 0);

		//}
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
	case 5:
		Pop();
		break;
	default:
		if (!(type >= 1024 && type < (1024 + maxArenas)))
		{
			Log("*error type", type);
			return;
		}
		ReferenceId(type - 1024);
		//Log("Arena Reference to spawn", type - 1024,(size_t)(arenaById[type - 1024]));
		current_arena = ((Arena*)arenaById[type - 1024])->SpawnArenaThread();
		assert(current_arena != nullptr);
		arenaStack.Push(current_arena);
		//Log("Arena reuse Push", arenaStack.Size());
		if (GetArenaStack().Size() > 1)
		{
			//Log("over Arena reuse Push", GetArenaStack().Size());
		}
	}
}


Arena* ArenaManager::MakeArena()
{
	unsigned int id = getId();
	Arena* arena = Arena::MakeArena(IdToAddress(id));
	//Log("Arena is registered ", (size_t)arena);
	arenaById[id] = arena;
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
	}
	else {
		//Log("GC Pop", GetArenaStack().Size());
	}
}


void* ArenaManager::GetArena()
{
	return GetArenaStack().Current();
}

unsigned int ArenaManager::Id(void * allocator)
{
	return (((size_t)allocator) >> arenaAddressShift) & (ArenaMask);
}

void ArenaManager::DeleteAllocator(void* vallocator)
{
	if (vallocator == nullptr) return;

	int id = Id(vallocator);
	Arena* allocator = static_cast<Arena*> (vallocator);

	// do not need to delete, because arena object is embedded in arena memory.
	allocator->Destroy();
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
	unsigned int id = (iaddr >> arenaAddressShift) & ArenaMask;
	//Log("LookupId", id, (size_t)arenaById[id]);
	return arenaById[id];
}

void* ArenaManager::Peek()
{
	ArenaThread* arena = (ArenaThread*)GetArenaStack().Current();
	if (arena == nullptr)
	{
		return nullptr;
	}
	return (arena)->Allocate(0);
}
int cnty = 0;
int cntx = 0;
void* ArenaManager::Allocate(size_t jsize)
{
	ArenaThread* arena = (ArenaThread*)GetArenaStack().Current();
	if (arena == nullptr)
	{
		return nullptr;
	}

	size_t size = Align(jsize) + headerSize;
	void*ret = (arena)->Allocate(size);
	if ((size_t)ret >= 0x40100010000)
	{
		//Log("trigger", (size_t)ret);
	}
	//	memset(ret, 0, size); it already is zero
	return (void*)((char*)ret + headerSize);
}

void* ArenaManager::Allocate(ArenaThread* arena, size_t jsize)
{
	size_t size = Align(jsize) + headerSize;
	void*ret = (arena)->Allocate(size);
	memset(ret, 0, size);
	return (void*)((char*)ret + headerSize);
}

int ArenaManager::lcnt = 0;
void ArenaManager::Log(char *str, size_t n, size_t n2, char*hdr, size_t n3)
{
#ifdef LOGGING
	if (hFile == (HANDLE)0xffffffff || hFile == 0)
	{
		hFile = GetStdHandle(STD_OUTPUT_HANDLE);
		//hFile = ::CreateFileA("log.txt",                // name of the write
		//	GENERIC_WRITE,          // open for writing
		//	FILE_SHARE_READ,                      // do not share
		//	NULL,                   // default security
		//	CREATE_ALWAYS,             // create new file only
		//	FILE_ATTRIBUTE_NORMAL,  // normal file
		//	NULL);                  // no attr. template
	}

	auto tid = GetCurrentThreadId();

	auto& arenaStack = GetArenaStack();
	if (*str == '*')
	{
		tid = GetCurrentThreadId();
	}

	char pbuf[1024];
	size_t pbufI = 0;
	DWORD written;

	_itoa(lcnt++, pbuf, 10);
	pbufI = strlen(pbuf); 
	//pbuf[pbufI++] = ',';
	//_itoa(WKS::hcnt, pbuf + pbufI, 10);
	//pbufI = strlen(pbuf);
	pbuf[pbufI++] = '[';

	_itoa(tid, pbuf + pbufI, 10);
	pbufI = strlen(pbuf);

	pbuf[pbufI++] = '@';
	_itoa(arenaStack.Size(), pbuf + pbufI, 10);
	pbufI = strlen(pbuf);
	if (arenaStack.Current() != nullptr)

	{
		pbuf[pbufI++] = '#';
	}

	pbuf[pbufI++] = ']';
	pbuf[pbufI++] = ' ';
	pbuf[pbufI++] = 'L';
	pbuf[pbufI++] = 'o';
	pbuf[pbufI++] = 'g';
	pbuf[pbufI++] = ' ';

	if (hdr != nullptr)
	{
		memcpy(pbuf + pbufI, hdr, strlen(hdr));
		pbufI += strlen(hdr);
		pbuf[pbufI++] = ' ';
		pbuf[pbufI++] = '=';
		pbuf[pbufI++] = ' ';
	}

	memcpy(pbuf + pbufI, str, strlen(str));
	pbufI += strlen(str);

	if (n != 0)
	{
		pbuf[pbufI++] = ':';
		pbuf[pbufI++] = ' ';
		pbuf[pbufI++] = '0';
		pbuf[pbufI++] = 'x';
		_ui64toa(n, pbuf + pbufI, 16);
		pbufI = strlen(pbuf);
	}

	if (n2 != 0)
	{
		pbuf[pbufI++] = ' ';
		pbuf[pbufI++] = '-';
		pbuf[pbufI++] = '>';
		pbuf[pbufI++] = ' ';
		pbuf[pbufI++] = '0';
		pbuf[pbufI++] = 'x';
		_ui64toa(n2, pbuf + pbufI, 16);
		pbufI = strlen(pbuf);
	}

	if (n3 != 0)
	{
		pbuf[pbufI++] = ' ';
		pbuf[pbufI++] = '@';
		pbuf[pbufI++] = '>';
		pbuf[pbufI++] = ' ';
		pbuf[pbufI++] = '0';
		pbuf[pbufI++] = 'x';
		_ui64toa(n3, pbuf + pbufI, 16);
		pbufI = strlen(pbuf);
	}

	pbuf[pbufI++] = '\n';

	WriteFile(hFile, pbuf, (DWORD)pbufI, &written, 0);
	//FlushFileBuffers(hFile);
#endif
}

#define header(i) ((CObjectHeader*)(i))
bool show = false;

alloc_context* GetThreadAllocContext();

#define ROUNDSIZE(x) (((size_t)(x)+7)&~7)
void*  ArenaManager::ArenaMarshal(void*vdst, void*vsrc)
{
	if (vsrc == nullptr) return nullptr;
#ifdef LOGGING
	VerifyObject((Object*)vsrc);
#endif
	// src Allocator is not used
	Arena* srcAllocator = (Arena*)AllocatorFromAddress(vsrc);
	Arena* dstAllocator = (Arena*)AllocatorFromAddress(vdst);

	// overrunning allocator... ignore.
	if (srcAllocator != nullptr && dstAllocator != nullptr) return vsrc;
	Object* src = (Object*)vsrc;

	MethodTable* mT;
	size_t cnt = 0;
	mT = src->GetMethodTable();
#ifdef LOGGING
	START_NOT_ARENA_SECTION
	DefineFullyQualifiedNameForClass();
	char* name = (char*)GetFullyQualifiedNameForClass(mT);
	END_NOT_ARENA_SECTION
#else
	char* name = "?";
#endif

	cnt = (mT->GetBaseSize() +
		(mT->HasComponentSize() ?
			((size_t)(src->GetNumComponents() * mT->RawGetComponentSize())) : 0));

	size_t size = ROUNDSIZE(cnt) + sizeof(Object) + headerSize;
#ifdef LOGGING
		Log("PreMarshall", (size_t)vsrc, (size_t)vdst, name, size);
#endif
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
		p = dstAllocator->ThreadSafeAllocate(size);
	}

	*(size_t*)p = 0;
	p = (void*)((size_t)p + headerSize);

	//Log("Cloned object allocated ", (size_t)p, size);
	// We know this is not a ByValueClass, but we must do our own reflection, so we start with copying the whole
	// class, and we will fix the fields that are not value v
	//Log("memwrite", (size_t)p, (size_t)p + size);
	PTR_MethodTable pMT = src->GetMethodTable();
	if (pMT->IsArray())
	{
		if (dstAllocator == nullptr)
		{
			Object* clone = (Object*)p;
			GCPROTECT_BEGIN(clone);
			GCPROTECT_BEGIN(src);
			for (size_t*ip = (size_t*)vsrc, *op = (size_t*)p; ip < (size_t*)((char*)vsrc + size); ) *op++ = *ip++;
			CloneArray(p, src, pMT, sizeof(ArrayBase), size);
			GCPROTECT_END();
			GCPROTECT_END();
		}
		else {
			Object* clone = (Object*)p;
			GCPROTECT_BEGIN(clone);
			GCPROTECT_BEGIN(src);
			for (size_t*ip = (size_t*)vsrc, *op = (size_t*)p; ip < (size_t*)((char*)vsrc + size); ) *op++ = *ip++;
			CloneArray(p, src, pMT, sizeof(ArrayBase), size);
			GCPROTECT_END();
			GCPROTECT_END();
		}
	}
	else
	{
		if (dstAllocator == nullptr)
		{
			Object* clone = (Object*)p;
			GCPROTECT_BEGIN(clone);
			GCPROTECT_BEGIN(src);
			for (size_t*ip = (size_t*)vsrc, *op = (size_t*)p; ip < (size_t*)((char*)vsrc + size); ) *op++ = *ip++;
			size_t headerSize = (char*)(src->GetData()) - (char*)src;
			CloneClass(p, src, pMT, (int)headerSize, size - headerSize);
			GCPROTECT_END();
			GCPROTECT_END();
		}
		else
		{
			Object* clone = (Object*)p;
			GCPROTECT_BEGIN(clone);
			GCPROTECT_BEGIN(src);
			for (size_t*ip = (size_t*)vsrc, *op = (size_t*)p; ip < (size_t*)((char*)vsrc + size); ) *op++ = *ip++;
			size_t headerSize = (char*)(src->GetData()) - (char*)src;
			CloneClass(p, src, pMT, (int)headerSize, size - headerSize);
			GCPROTECT_END();
			GCPROTECT_END();
		}
	}
#ifdef LOGGING

		Log("ArenaMarshal clone", (size_t)src, (size_t)p, name, (size_t)vdst);
		VerifyObject((Object*)p);
#endif
		return p;
}

void ArenaManager::CloneArray(void* dst, Object* src, PTR_MethodTable pMT, int ioffset, size_t size)
{

	TypeHandle arrayTypeHandle = ArrayBase::GetTypeHandle(pMT);
	ArrayTypeDesc* ar = arrayTypeHandle.AsArray();
	TypeHandle ty = ar->GetArrayElementTypeHandle();
	int componentSize = pMT->RawGetComponentSize();
	ArrayBase* refArray = (ArrayBase*)src;
	DWORD numComponents = refArray->GetNumComponents();
	const CorElementType arrayElType = ty.GetVerifierCorElementType();

	switch (arrayElType) {

	case ELEMENT_TYPE_VALUETYPE:
		for (size_t i = ioffset; i < ioffset + numComponents*componentSize; i += componentSize)
		{
			CloneClass((char*)dst + i , (Object*)((char*)src + i), ty.AsMethodTable(), 0, componentSize);
		}

		break;
	case ELEMENT_TYPE_STRING:
		break;
	case ELEMENT_TYPE_CLASS: // objectrefs
	{
		// because ArenaMarshal can trigger
		// a GC, it is necessary to put memory
		// in a GC safe state first.
		for (int i = ioffset; i < ioffset + numComponents*sizeof(void*); i += sizeof(void*))
		{
			void** rdst = (void**)((char*)dst + i);
			*rdst = nullptr;
		}

		for (int i = ioffset; i < ioffset + numComponents*sizeof(void*); i += sizeof(void*))
		{
			OBJECTREF *pSrc = *(OBJECTREF **)((char*)src + i);
			if (pSrc != nullptr && (size_t)pSrc < arenaRangeEnd)
			{
				void* clone = ArenaMarshal(dst, pSrc);
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
		break;
	}
}

void ArenaManager::CloneClass(void* dst, Object* src, PTR_MethodTable mt, int ioffset, size_t classSize)
{
#if DEBUG
	//Log((char*)mt->GetDebugClassName(), 0, 0, "cloneclass");
#endif

	auto pCurrMT = mt;
#ifdef LOGGING
	START_NOT_ARENA_SECTION
	DefineFullyQualifiedNameForClass();
	char* name = (char*)GetFullyQualifiedNameForClass(mt);
		Log("CloneClass", (size_t)src, classSize, name);
	END_NOT_ARENA_SECTION
#endif

	while (pCurrMT)
	{
		DWORD numInstanceFields = pCurrMT->GetNumInstanceFields();
		if (numInstanceFields == 0) break;

		FieldDesc *pSrcFields = pCurrMT->GetApproxFieldDescListRaw();
		if (pSrcFields == nullptr) break;

		for (DWORD i = 0; i < numInstanceFields; i++)
		{

			FieldDesc &f = pSrcFields[i];// pSrcFields[i];
			if (f.IsStatic()) continue;
			CorElementType type = f.GetFieldType();
			DWORD offset = f.GetOffset() + ioffset;
			if (offset >= classSize)
			{
				offset = f.GetOffset() + ioffset;
				continue;  // this seems like a bug, but we workaround for prototypes
			}

#ifdef DEBUG
			LPCUTF8 szFieldName = f.GetDebugName();
			//Log((char*)szFieldName, offset, 0, "field");
#endif


			switch (type) {

			case ELEMENT_TYPE_VALUETYPE:
			{

				TypeHandle th = LoadExactFieldType(&pSrcFields[i], mt, GetAppDomain());
				CloneClass1((char*)dst + offset, (Object*)((char*)src + offset), th.AsMethodTable(), 0, th.AsMethodTable()->GetBaseSize());
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

					void** rdst = (void**)((char*)dst + offset);
					// because ArenaMarshal can trigger a GC
					// it is necessary to put memory in a GC
					// safe state first.
					*rdst = nullptr;

				}
			}
			break;
			default:
				break;
			}
		}
		pCurrMT = pCurrMT->GetParentMethodTable();

	}

	pCurrMT = mt;
	while (pCurrMT)
	{
		DWORD numInstanceFields = pCurrMT->GetNumInstanceFields();
		if (numInstanceFields == 0) break;

		FieldDesc *pSrcFields = pCurrMT->GetApproxFieldDescListRaw();
		if (pSrcFields == nullptr) break;

		for (DWORD i = 0; i < numInstanceFields; i++)
		{
			FieldDesc &f = pSrcFields[i];// pSrcFields[i];

			if (f.IsStatic()) continue;
			CorElementType type = f.GetFieldType();
			DWORD offset = f.GetOffset() + ioffset;
			if (offset >= classSize)
			{
				continue;
			}

#ifdef DEBUG
			LPCUTF8 szFieldName = f.GetDebugName();
			//Log((char*)szFieldName, offset, 0, "field");
#endif


			switch (type) {

			case ELEMENT_TYPE_VALUETYPE:
			{
				TypeHandle th = LoadExactFieldType(&pSrcFields[i], mt, GetAppDomain());
				CloneClass((char*)dst + offset, (Object*)((char*)src + offset), th.AsMethodTable(), 0, th.AsMethodTable()->GetBaseSize());
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

					void** rdst = (void**)((char*)dst + offset);
					// because ArenaMarshal can trigger a GC
					// it is necessary to put memory in a GC
					// safe state first.
					void* clone = ArenaMarshal((void*)rdst, pSrc);
					*rdst = clone;
					//Log("ArenaMarshal ptr", (size_t)rdst, (size_t)clone);
				}
			}
			break;
			default:
				break;

			}
		}

		pCurrMT = pCurrMT->GetParentMethodTable();
	}
}

void ArenaManager::CloneClass1(void* dst, Object* src, PTR_MethodTable mt, int ioffset, size_t classSize)
{
#if DEBUG
	Log((char*)mt->GetDebugClassName(), 0, 0, "cloneclass");
#endif


	auto pCurrMT = mt;
	while (pCurrMT)
	{
		DWORD numInstanceFields = pCurrMT->GetNumInstanceFields();
		if (numInstanceFields == 0) break;

		FieldDesc *pSrcFields = pCurrMT->GetApproxFieldDescListRaw();
		if (pSrcFields == nullptr) break;

		for (DWORD i = 0; i < numInstanceFields; i++)
		{
			FieldDesc f = pSrcFields[i];
			if (f.IsStatic()) continue;
			CorElementType type = f.GetFieldType();
			DWORD offset = f.GetOffset() + ioffset;
			if (offset >= classSize)
			{
				continue;
			}

#ifdef DEBUG
			LPCUTF8 szFieldName = f.GetDebugName();
			//Log((char*)szFieldName, offset, 0, "field");
#endif


			switch (type) {

			case ELEMENT_TYPE_VALUETYPE:
			{

				TypeHandle th = LoadExactFieldType(&pSrcFields[i], mt, GetAppDomain());
				CloneClass1((char*)dst + offset, (Object*)((char*)src + offset), th.AsMethodTable(), 0, th.AsMethodTable()->GetBaseSize());
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

					void** rdst = (void**)((char*)dst + offset);
					// because ArenaMarshal can trigger a GC
					// it is necessary to put memory in a GC
					// safe state first.
					*rdst = nullptr;
					//Log("memwriteptr", (size_t)rdst, (size_t)clone);
				}
			}
			break;
			default:
				break;
			}
		}

		pCurrMT = pCurrMT->GetParentMethodTable();
	}

}

#ifdef LOGGING
void ArenaManager::VerifyObject(Object* o, MethodTable* pMT0)
{
	if (o == nullptr) return;
	MethodTable* pMT = pMT0;

	if (pMT == nullptr)
	{
		pMT = o->GetMethodTable();
	}
	else
	{
		// if value type, pMT is not at location 0, so back up the object pointer
		o = (Object*)((Object*)o - 1);
	}

	DefineFullyQualifiedNameForClass();
	char* name = (char*)GetFullyQualifiedNameForClass(pMT);
	Log("Verify Object ", (size_t)o, (size_t)pMT, name, (size_t)(pMT->GetBaseSize()));

	if (pMT->IsArray())
	{
		VerifyArray(o,pMT);
	}
	else
	{
		VerifyClass(o,pMT);
	}
}


void ArenaManager::VerifyClass(Object* o, MethodTable* pMT)
{
	auto pCurrMT = pMT;
	int ioffset = 8;
	int classSize = pCurrMT->GetBaseSize();
	while (pCurrMT)
	{
		DWORD numInstanceFields = pCurrMT->GetNumInstanceFields();
		if (numInstanceFields == 0) break;

		FieldDesc *pSrcFields = pCurrMT->GetApproxFieldDescListRaw();
		if (pSrcFields == nullptr) break;

		for (DWORD i = 0; i < numInstanceFields; i++)
		{
			FieldDesc f = pSrcFields[i];
			if (f.IsStatic()) continue;
			CorElementType type = f.GetFieldType();
			DWORD offset = f.GetOffset() + ioffset;
			if (offset >= (DWORD)classSize)
			{
				continue;
			}

#ifdef DEBUG
			LPCUTF8 szFieldName = f.GetDebugName();
			//Log((char*)szFieldName, offset, 0, "field");
#endif


			switch (type) {

			case ELEMENT_TYPE_VALUETYPE:
			{
				TypeHandle th = LoadExactFieldType(&pSrcFields[i], pCurrMT, GetAppDomain());
				
				Object* o2 = (Object*)((char*)o + offset);
				VerifyObject(o2,th.AsMethodTable());
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
				Object* o2 = *(Object**)((char*)o + offset);
				VerifyObject(o2);
			}
			break;
			default:
				break;
			}
		}

		pCurrMT = pCurrMT->GetParentMethodTable();
	}
}

void ArenaManager::VerifyArray(Object* o, MethodTable* pMT)
{
	int ioffset = 16;
	
	TypeHandle arrayTypeHandle = ArrayBase::GetTypeHandle(pMT);
	ArrayTypeDesc* ar = arrayTypeHandle.AsArray();
	TypeHandle ty = ar->GetArrayElementTypeHandle();
	int componentSize = pMT->RawGetComponentSize();
	ArrayBase* refArray = (ArrayBase*)o;
	DWORD numComponents = refArray->GetNumComponents();
	const CorElementType arrayElType = ty.GetVerifierCorElementType();

	switch (arrayElType) {

	case ELEMENT_TYPE_VALUETYPE:
		for (size_t i = ioffset; i < ioffset + numComponents*componentSize; i += componentSize)
		{
			VerifyObject((Object*)((char*)o + i), ty.AsMethodTable());
		}

		break;
	case ELEMENT_TYPE_STRING:
		break;
	case ELEMENT_TYPE_CLASS: // objectrefs
	{
		for (int i = ioffset; i < ioffset + numComponents*sizeof(void*); i += sizeof(void*))
		{
			Object* o2 = *(Object**)((char*)o + i);
			VerifyObject(o2);
		}	
	}

	break;
	case ELEMENT_TYPE_OBJECT:
		for (int i = ioffset; i < ioffset + numComponents*sizeof(void*); i += sizeof(void*))
		{
			Object* o2 = *(Object**)((char*)o + i);
			VerifyObject(o2);
		}			break;
	case ELEMENT_TYPE_SZARRAY:      // single dim, zero
		for (int i = ioffset; i < ioffset + numComponents*sizeof(void*); i += sizeof(void*))
		{
			Object* o2 = *(Object**)((char*)o + i);
			VerifyObject(o2);
		}			break;
	case ELEMENT_TYPE_ARRAY:        // all other arrays
									// this is where we recursively follow
		for (int i = ioffset; i < ioffset + numComponents*sizeof(void*); i += sizeof(void*))
		{
			Object* o2 = *(Object**)((char*)o + i);
			VerifyObject(o2);
		}	
		break;
	default:
		break;
	}
}

#endif

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
	if (allocator == nullptr)
	{
		return nullptr;
	}
	return ((ArenaThread*)allocator)->Allocate(len);
}