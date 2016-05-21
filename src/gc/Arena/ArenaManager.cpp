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
#include "set.h"
#include "vector.h"
#include "hashtable.h"

#define LOGGING

ArenaVirtualMemoryState ArenaVirtualMemory::s;

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
set verifiedObjects;

void ArenaManager::InitArena()
{
#ifdef DEBUG
	vector<int>::Test();
	hashtable::Test();
	set::Test();
#endif

	for (int i = 0; i < maxArenas; i++)
	{
		arenaById[i] = nullptr;
		refCount[i] = 0;
	}

	ArenaVirtualMemory::Initialize();
#ifdef VERIFYALLOC
	ClrVirtualAlloc((LPVOID)0x60000000000, 1024 * 1024 * 1024, MEM_COMMIT, PAGE_READWRITE);
	*(size_t*)0x60000000000 = 0x60000000008;
#endif
}

void ArenaManager::CheckAll()
{
#ifdef VERIFYALLOC
	verifiedObjects.clear();
	auto p = (size_t**)0x60000000000;
	for (auto i = (size_t*)0x60000000008; i < *p; i++)
	{
		VerifyObject(*(Object**)i);
	}
#endif
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

ArenaId _cdecl ArenaManager::GetArenaId()
{
	Arena* arena = (Arena*)GetArenaStack().Current();
	if (arena == nullptr) return -1;
	return ArenaVirtualMemory::ArenaNumber(arena);
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
			DereferenceId(ArenaVirtualMemory::ArenaNumber(arenaStack[i]));
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

void* ArenaManager::GetBuffer(ArenaId arenaId, size_t len)
{
	return ArenaVirtualMemory::GetBuffer(arenaId, len);
}

Arena* ArenaManager::MakeArena()
{
	ArenaId id = getId();
	void* arenaBase = ArenaVirtualMemory::GetBuffer(id);

	Arena* arena = Arena::MakeArena(id, (size_t)arenaBase, ArenaVirtualMemory::bufferSize, ArenaVirtualMemory::arenaBaseSize / 4);
	//Log("Arena is registered ", (size_t)arena);
	arenaById[id] = arena;
	return arena;
}


void ArenaManager::DeleteAllocator(void* vallocator)
{
	if (vallocator == nullptr) return;

	ArenaId id = ArenaVirtualMemory::ArenaNumber(vallocator);
	Arena* allocator = static_cast<Arena*> (vallocator);

	// do not need to delete, because arena object is embedded in arena memory.
	allocator->Destroy();
}

inline void* ArenaManager::Peek()
{
	ArenaThread* arena = (ArenaThread*)GetArenaStack().Current();
	if (arena == nullptr)
	{
		return nullptr;
	}
	return (arena)->Allocate(0);
}

void  ArenaManager::RegisterForFinalization(Object* o, size_t size)
{
	Log("RegisterForFinalization", (size_t)o, (size_t)size);
}

void* ArenaManager::Allocate(size_t jsize, uint32_t flags)
{
	ArenaThread* arena = (ArenaThread*)GetArenaStack().Current();
	if (arena == nullptr)
	{
		return nullptr;
	}

	size_t size = Align(jsize);
	void*ret = (arena)->Allocate(size);
	memset(ret, 0, size);
	if (flags & GC_ALLOC_FINALIZE)
	{
		RegisterForFinalization((Object*)ret, size);
	}
	return (void*)((char*)ret);
}

void* ArenaManager::Allocate(ArenaThread* arena, size_t jsize)
{
	size_t size = Align(jsize);
	void*ret = (arena)->Allocate(size);

	memset(ret, 0, size);
	return (void*)((char*)ret);
}

//namespace WKS
//{
//	extern int hcnt;
//}

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

alloc_context* GetThreadAllocContext();

#define ROUNDSIZE(x) (((size_t)(x)+7)&~7)
struct MarshalRequest
{
	Object** dst;
	Object* src;
	size_t valueTypeSize;
	MethodTable* pMT;

	MarshalRequest(Object*csrc, Object**cdst)
	{
		dst = cdst;
		src = csrc;
		valueTypeSize = 0;
		pMT = nullptr;
	}

	MarshalRequest(Object*csrc, Object**cdst, size_t ivalueTypeSize, MethodTable* mt)
	{
		dst = cdst;
		src = csrc;
		valueTypeSize = ivalueTypeSize;
		pMT = mt;
	}

	MarshalRequest()
	{
		dst = 0;
		src = 0;
	}

	bool operator==(MarshalRequest& ref)
	{
		return (ref.dst == dst && ref.src == src && ref.pMT == pMT && ref.valueTypeSize == valueTypeSize);
	}
};

// We may need to recursively marshal objects, however we cannot use recursive functions
// because we operate in cooperative mode with the GC, which means that we must be GC safe
// as of the execution of each 'ret' instruction.  Thus we will use a queue to manage recursion.
// ALL methods called must be inline methods.  LOGGING is not inline, so can create crashes, and
// is only used for diagnostics.
void*  ArenaManager::ArenaMarshal(void*idst, void*isrc)
{
	STATIC_CONTRACT_MODE_COOPERATIVE;
	STATIC_CONTRACT_NOTHROW;
	STATIC_CONTRACT_GC_TRIGGERS;
	STATIC_CONTRACT_SO_TOLERANT;

	if (isrc == nullptr) return nullptr;
	Thread* thread = GetThread();

	vector<MarshalRequest> queue;
	queue.push_back(MarshalRequest((Object*)isrc, (Object**)idst));
	Object* firstClone = nullptr;

	while (!queue.empty())
	{
		MarshalRequest request = queue.pop_front();
		queue.delete_repeat(request);

		Object *src = request.src;
		Object **dst = request.dst;

		// if the destination address is in a GC HEAP, then dstAllocator == nullptr
		Arena* dstAllocator = (Arena*)AllocatorFromAddress(dst);
		MethodTable* pMT = request.pMT == nullptr ? src->GetMethodTable() : request.pMT;

		// valueTypeSize is nonzero only for structs within either classes or arrays
		size_t valueTypeSize = request.valueTypeSize;

		// size includes header, methodtable and object
		size_t size = (pMT->GetBaseSize() +
			(pMT->HasComponentSize() ?
				((size_t)(src->GetNumComponents() * pMT->RawGetComponentSize())) : 0));
		size = ROUNDSIZE(size);

#ifdef LOGGING
		PushGC(thread);
		DefineFullyQualifiedNameForClass();
		char* name = (char*)GetFullyQualifiedNameForClass(pMT);
		Pop(thread);
#endif

		// Allocate memory for clone from either GC or arena
		Object* clone = nullptr;
		if (request.valueTypeSize != 0)
		{
			clone = (Object*)request.dst;
		}
		else {
			if (dstAllocator == nullptr)
			{
				PushGC(thread);
				DWORD flags = ((pMT->ContainsPointers() ? GC_ALLOC_CONTAINS_REF : 0) | (pMT->HasFinalizer() ? GC_ALLOC_FINALIZE : 0));
				if (GCHeap::UseAllocationContexts())
					clone = (Object*)GCHeap::GetGCHeap()->Alloc(GetThreadAllocContext(), size, flags);
				else
					clone = (Object*)GCHeap::GetGCHeap()->Alloc(size, flags);
				// Pop is guaranteed not to call a method on the stack if it follows a PushGC();
				Pop(thread);
			}
			else
			{
				clone = (Object*)dstAllocator->ThreadSafeAllocate(size);
			}
			// copy the message table pointer  leave the rest zero.
			*(size_t*)clone = *(size_t*)src;
		}
		if (firstClone == nullptr)
		{
			firstClone = clone;
		}

		if (pMT == g_pStringClass)
		{
			size_t* csrc = (size_t*)src;
			size_t* srcEnd = (size_t*)((char*)src + size);
			size_t* cdst = (size_t*)clone;
			while (csrc < srcEnd) *cdst++ = *csrc++;
		}
		else if (pMT->IsArray())
		{
			// copy length word

			int ioffset = pMT->GetBaseSize();
#ifdef AMD64
			ioffset -= 8;
#else
			ioffset -= 4;
#endif

			memcpy(clone, src, ioffset);
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
					queue.push_back(
						MarshalRequest(
							(Object*)((char*)src + i),
							(Object**)((char*)clone + i),
							componentSize,
							ty.AsMethodTable()));
				}

				break;
			case ELEMENT_TYPE_STRING:
			case ELEMENT_TYPE_CLASS: // objectrefs
			{
				// because ArenaMarshal can trigger
				// a GC, it is necessary to put memory
				// in a GC safe state first.
				for (int i = ioffset; i < ioffset + numComponents*sizeof(void*); i += sizeof(void*))
				{
					void** rdst = (void**)((char*)clone + i);
					*rdst = nullptr;
				}

				for (int i = ioffset; i < ioffset + numComponents*sizeof(void*); i += sizeof(void*))
				{
					Object *pSrc = *(Object **)((char*)src + i);
					Object **pDst = (Object **)((char*)clone + i);
					if (pSrc != nullptr)
					{
						queue.push_back(MarshalRequest(pSrc, pDst));
					}
				}
			}

			break;
			case ELEMENT_TYPE_OBJECT:
				DebugBreak();
				break;
			case ELEMENT_TYPE_SZARRAY:      // single dim, zero
				DebugBreak();
				break;
			case ELEMENT_TYPE_ARRAY:        // all other arrays
											// this is where we recursively follow
				DebugBreak();
				break;
			default:
			{
				size_t* pSrc = (size_t*)src;
				size_t* pSrcEnd = (size_t*)((char*)src + size);
				size_t* pDst = (size_t*)clone;
				for (; pSrc < pSrcEnd;) *pDst++ = *pSrc++;
			}
			break;
			}
		}
		else  // Clone Class
		{
			size_t ioffset = (char*)(src->GetData()) - (char*)src;
			size_t cloneSize = size - ioffset;
			if (valueTypeSize != 0)
			{
				ioffset = 0;
				cloneSize = valueTypeSize;
			}

#ifdef LOGGING
			PushGC(thread);
			DefineFullyQualifiedNameForClass();
			char* name = (char*)GetFullyQualifiedNameForClass(pMT);
			Pop(thread);
#endif
			auto pCurrMT = pMT;
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
					size_t offset = f.GetOffset() + ioffset;
					if (pSrcFields[i].IsNotSerialized())
					{
						Log("IsNotSerialized", offset);
						continue;
					}

					if (offset >= cloneSize)
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
						TypeHandle th = LoadExactFieldType(&pSrcFields[i], pMT, GetAppDomain());
						queue.push_back(
							MarshalRequest(
								(Object*)((char*)src + offset),
								(Object**)((char*)clone + offset),
								th.AsMethodTable()->GetBaseSize(),
								th.AsMethodTable()
								));
					}
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
							void** rdst = (void**)((char*)clone + offset);
							queue.push_back(MarshalRequest(
								(Object*)pSrc,
								(Object**)rdst));
						}
					}
					break;
					case ELEMENT_TYPE_BOOLEAN:
					case ELEMENT_TYPE_I1:
					case ELEMENT_TYPE_U1:
					{
						char *pSrc = ((char*)src + offset);
						char* pDst = ((char*)clone + offset);
						*pDst = *pSrc;
					}
					break;

					case ELEMENT_TYPE_CHAR:
					case ELEMENT_TYPE_I2:
					case ELEMENT_TYPE_U2:
					{
						short *pSrc2 = (short*)((char*)src + offset);
						short* pDst2 = (short*)((char*)clone + offset);
						*pDst2 = *pSrc2;
					}
					break;

					case ELEMENT_TYPE_I4:
					case ELEMENT_TYPE_U4:
					case ELEMENT_TYPE_R4:
					{
						int *pSrc4 = (int*)((char*)src + offset);
						int* pDst4 = (int*)((char*)clone + offset);
						*pDst4 = *pSrc4;
					}
					break;
					case ELEMENT_TYPE_PTR:
					case ELEMENT_TYPE_FNPTR:
						// pointers may be dangerous, but they are
						// copied without chang

					case ELEMENT_TYPE_I8:
					case ELEMENT_TYPE_U8:
					case ELEMENT_TYPE_R8:
					{
						size_t *pSrc8 = (size_t*)((char*)src + offset);
						size_t* pDst8 = (size_t*)((char*)clone + offset);
						*pDst8 = *pSrc8;
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

		Log("ArenaMarshal clone", (size_t)src, (size_t)clone, name, (size_t)dst);
#endif
#ifdef VERIFYALLOC
		if (!ISARENA(clone))
		{
			RegisterAddress(clone);
		}
		CheckAll();
#endif

		if (valueTypeSize == 0)
		{
			*dst = clone;
			if (dstAllocator == nullptr)
			{

#ifdef _DEBUG
				Thread::ObjectRefAssign((OBJECTREF *)dst);
#endif
				ErectWriteBarrier(dst, clone);
			}
		}
	}
	return firstClone;
}

#ifdef LOGGING
void ArenaManager::VerifyObject(Object* o, MethodTable* pMT0)
{
	if (o == nullptr) return;
	if (verifiedObjects.contains(o))
	{

		return;
	}
	verifiedObjects.add(o);
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

	//DefineFullyQualifiedNameForClass();
	//char* name = (char*)GetFullyQualifiedNameForClass(pMT);
	//Log("Verify Object ", (size_t)o, (size_t)pMT, name, (size_t)(pMT->GetBaseSize()));

	if (pMT->IsArray())
	{
		VerifyArray(o, pMT);
	}
	else
	{
		VerifyClass(o, pMT);
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
			if (pSrcFields[i].IsNotSerialized())
			{
				Log("IsNotSerialized", offset);
				continue;
			}
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
				VerifyObject(o2, th.AsMethodTable());
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
			case ELEMENT_TYPE_VAR:
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