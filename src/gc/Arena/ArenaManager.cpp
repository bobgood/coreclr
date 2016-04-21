// Hack
#include "../common.h"
#include "arenaManager.h"
#include "ArenaAllocator.h"
#include "ArenaHeader.h"
#include "vector.h"
#include "../tls.h"
#include "../object.h"

#define FEATURE_IMPLICIT_TLS
#include "../threads.h"

// thread_local data. 

// arena is the currently assigned allocator (null if not arena).  This variable is available to MASM code
THREAD_LOCAL void* ArenaManager::arena = nullptr;

// the stack of allocators.  The top of stack is always represented with arena.
THREAD_LOCAL void** ArenaManager::arenaStack = nullptr;
THREAD_LOCAL int ArenaManager::arenaStackI = 0;

size_t ArenaManager::virtualBase;


// lastId keeps track of the last id used to create an arena.  
// the next one allocated will try at lastId+1, and keep trying
// until an empty one is found.  this means that ids will not be
// reused immediately, which means that a buffer becomes unaccessable
// for as long as possible after it is free.

unsigned int ArenaManager::lastId = 0;
void* ArenaManager::arenaById[maxArenas];
bool ArenaManager::idInUse[maxArenas];

void ArenaManager::InitArena()
{
	for (int i = 0; i < maxArenas; i++)
	{
		arenaById[i] = nullptr;
		idInUse[i] = false;
	}

	lastId = 0;
	virtualBase = (size_t)ClrVirtualAlloc((LPVOID)arenaBaseRequest, maxArenas * (1ULL << arenaAddressShift), MEM_RESERVE, PAGE_NOACCESS);
	if (virtualBase != arenaBaseRequest)
	{
		throw "could not reserve virtual address space for arena buffers";
	}
}

void ArenaManager::ReleaseId(int id)
{
	assert(arenaById[id]);
	idInUse[id] = false;
	arenaById[id] = nullptr;
}

int ArenaManager::getId()
{
	int id = lastId + 1;
	int cnt = 0;
	for (int id = lastId + 1; cnt < maxArenas; id = (id + 1) % maxArenas, cnt++)
	{
		if (!idInUse[id])
		{
			auto was = lastId;
			if (was == InterlockedCompareExchange(&lastId, id, was))
			{
				idInUse[id] = true;
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

// Hack
// 1 = reset to GCHeap
// 2 = push new arena allocator
// 3 = push GCHeap
// 4 = pop
void _cdecl ArenaManager::SetAllocator(unsigned int type)
{


	if (arenaStack == nullptr)
	{
		arenaStack = new void*[10];
	}
	assert(type >= 1 && type <= 4);
	switch (type)
	{
	case 1:
		for (int i = 0; i < arenaStackI; i++) DeleteAllocator(arenaStack[i]);
		arenaStackI = 0;
		arenaStack[arenaStackI++] = nullptr;
		arena = nullptr;
		break;
	case 2:
		arena = MakeArena();
		arenaStack[arenaStackI++] = arena;
		break;
	case 3:
		PushGC();
		break;
	case 4:
		Pop();
		break;
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
	if (ArenaManager::arenaStack == nullptr) return;
	::ArenaManager::Log("Push");
	arenaStack[arenaStackI++] = nullptr;
	if (arenaStackI > 7) { Log("arenastack overflow"); }
	arena = nullptr;
}

int Popcnt = 0;
void _cdecl ArenaManager::Pop()
{
	if (ArenaManager::arenaStack == nullptr) return;
	::ArenaManager::Log("Pop");
	Popcnt++;
	void* allocator = arenaStack[--arenaStackI];
	DeleteAllocator(allocator);
	arena = nullptr;
	if (arenaStackI > 0) {
		arena = arenaStack[arenaStackI - 1];
	}
}

void* ArenaManager::GetArena()
{
	return arena;
}

unsigned int ArenaManager::Id(void * allocator)
{
	return (((Arena*)allocator)->m_config.addr >> arenaAddressShift) & (maxArenas - 1);
}

void ArenaManager::DeleteAllocator(void* vallocator)
{
	Arena* allocator = static_cast<Arena*> (vallocator);
	if (allocator == nullptr) return;
	ReleaseId(Id(allocator));
	delete allocator;
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

void* ArenaManager::Allocate(size_t jsize)
{
	if (arena == nullptr)
	{
		return nullptr;
	}
	size_t size = Align(jsize);
	return ((Arena*)arena)->Allocate(size);
}
int lcnt = 0;
void ArenaManager::Log(char *str, size_t n)
{
	DWORD written;
	char bufn[25];
	_itoa(lcnt++, bufn, 10);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bufn, (DWORD)strlen(bufn), &written, 0);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), " Log ", (DWORD)strlen(" Log "), &written, 0);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), str, (DWORD)strlen(str), &written, 0);
	if (n != 0)
	{
		char buf[25];
		buf[0] = ':'; buf[1] = ' ';
		buf[2] = '0'; buf[3] = 'x';
		_ui64toa(n, buf + 4, 16);
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)strlen(buf), &written, 0);
	}
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\n", (DWORD)strlen("\n"), &written, 0);
}

alloc_context* GetThreadAllocContext();

void*  ArenaManager::ArenaMarshall(void*vdst, void*vsrc)
{
	ArenaManager::Log("ArenaMarshall", (size_t)vsrc);

	// src Allocator is not used
	Arena* srcAllocator = (Arena*)AllocatorFromAddress(vsrc);
	Arena* dstAllocator = (Arena*)AllocatorFromAddress(vdst);

	Object* src = (Object*)vsrc;
	size_t size = src->GetSize();
	void *p = nullptr;
	bool bContainsPointers = false;
	bool bFinalize = false;
	if (dstAllocator == nullptr)
	{
		DWORD flags = ((bContainsPointers ? GC_ALLOC_CONTAINS_REF : 0) |
			(bFinalize ? GC_ALLOC_FINALIZE : 0));

		if (GCHeap::UseAllocationContexts())
			p = GCHeap::GetGCHeap()->Alloc(GetThreadAllocContext(), size, flags);
		else
			p = GCHeap::GetGCHeap()->Alloc(size, flags);
	}
	else
	{
		p = dstAllocator->Allocate(size);
	}

	// We know this is not a ByValueClass, but we must do our own reflection, so we start with copying the whole
	// class, and we will fix the fields that are not value v
	for (char*ip = (char*)vsrc, *op = (char*)p; ip < (char*)vsrc + size; ) *op++ = *ip++;

	PTR_MethodTable mt = src->GetMethodTable();
	if (mt->IsArray())
	{

		TypeHandle arrayTypeHandle = src->GetGCSafeTypeHandle();
		ArrayTypeDesc* ar = arrayTypeHandle.AsArray();
		TypeHandle ty = ar->GetArrayElementTypeHandle();
		const CorElementType arrayElType = ty.GetVerifierCorElementType();

		switch (arrayElType) {

		case ELEMENT_TYPE_I1:
		case ELEMENT_TYPE_U1:
		case ELEMENT_TYPE_BOOLEAN:
			//*retVal = IndexOfUINT8((U1*)array->GetDataPtr(), index, count, *(U1*)value->UnBox());
			break;

		case ELEMENT_TYPE_I2:
		case ELEMENT_TYPE_U2:
		case ELEMENT_TYPE_CHAR:
			//*retVal = ArrayHelpers<U2>::IndexOf((U2*)array->GetDataPtr(), index, count, *(U2*)value->UnBox());
			break;

		case ELEMENT_TYPE_I4:
		case ELEMENT_TYPE_U4:
		case ELEMENT_TYPE_R4:
			IN_WIN32(case ELEMENT_TYPE_I:)
				IN_WIN32(case ELEMENT_TYPE_U:)
				break;

				case ELEMENT_TYPE_I8:
				case ELEMENT_TYPE_U8:
				case ELEMENT_TYPE_R8:
					IN_WIN64(case ELEMENT_TYPE_I:)
						IN_WIN64(case ELEMENT_TYPE_U:)
						case ELEMENT_TYPE_FNPTR:
							//*retVal = ArrayHelpers<U8>::IndexOf((U8*)array->GetDataPtr(), index, count, *(U8*)value->UnBox());
							break;

						case ELEMENT_TYPE_PTR:
							break;
						case ELEMENT_TYPE_STRING:
						case ELEMENT_TYPE_CLASS: // objectrefs
						case ELEMENT_TYPE_OBJECT:
						case ELEMENT_TYPE_SZARRAY:      // single dim, zero
						case ELEMENT_TYPE_ARRAY:        // all other arrays
							// this is where we recursively follow
							break;
						default:
							_ASSERTE(!"Unrecognized primitive type in ArrayHelper::TrySZIndexOf");
							return p;
		}
	}
	else
	{
		DWORD numInstanceFields = mt->GetNumInstanceFields();
		if (numInstanceFields == 0) return p;
		FieldDesc *pSrcFields = mt->GetApproxFieldDescListRaw();
		for (DWORD i = 0; i < numInstanceFields; i++)
		{
			FieldDesc f = pSrcFields[i];
			if (f.IsStatic()) continue;
			CorElementType type = f.GetFieldType();
			switch (type) {
			case ELEMENT_TYPE_FNPTR:
				Log("ELEMENT_TYPE_FNPTR");
				break;
			case ELEMENT_TYPE_PTR:
				Log("ELEMENT_TYPE_PTR");
				break;
			case ELEMENT_TYPE_STRING:
				Log("ELEMENT_TYPE_STRING");
				break;
			case ELEMENT_TYPE_CLASS: // objectrefs
				Log("ELEMENT_TYPE_CLASS");
				break;
			case ELEMENT_TYPE_OBJECT:
				Log("ELEMENT_TYPE_OBJECT");
				break;
			case ELEMENT_TYPE_SZARRAY:      // single dim, zero
				Log("ELEMENT_TYPE_SZARRAY");
				break;
			case ELEMENT_TYPE_ARRAY:        // all other arrays
				Log("ELEMENT_TYPE_ARRAY");
				break;
			default:
				Log("default", (size_t)type);
				break;

			}
		}
	}


	return p;
}

void* RunAllocator(void* allocator, size_t len)
{
	return ((Arena*)allocator)->Allocate(len);
}