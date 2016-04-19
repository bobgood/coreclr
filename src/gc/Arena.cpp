// Hack
#include "common.h"
#include "arena.h"
#include "ArenaAllocator.h"
#include "vector.h"

// thread_local data. 

// arena is the currently assigned allocator (null if not arena).  This variable is available to MASM code
THREAD_LOCAL void* ArenaControl::arena = nullptr;

// the stack of allocators.  The top of stack is always represented with arena.
THREAD_LOCAL void** ArenaControl::arenaStack = nullptr;
THREAD_LOCAL int ArenaControl::arenaStackI = 0;

size_t ArenaControl::virtualBase;


typedef sfl::ArenaAllocator Arena;


// lastId keeps track of the last id used to create an arena.  
// the next one allocated will try at lastId+1, and keep trying
// until an empty one is found.  this means that ids will not be
// reused immediately, which means that a buffer becomes unaccessable
// for as long as possible after it is free.

unsigned int ArenaControl::lastId = 0;
bool ArenaControl::idInUse[maxArenas];

void ArenaControl::InitArena()
{
	for (int i = 0; i < maxArenas; i++)
	{
		idInUse[i] = false;
	}

	lastId = 0;
	virtualBase = (size_t)ClrVirtualAlloc((LPVOID)arenaBaseRequest, maxArenas * (1ULL << arenaAddressShift), MEM_RESERVE, PAGE_NOACCESS);
	if (virtualBase != arenaBaseRequest)
	{
		throw "could not reserve virtual address space for arena buffers";
	}
}

void ArenaControl::ReleaseId(int id)
{
	assert(idInUse[id]);
	idInUse[id] = false;
}

int ArenaControl::getId()
{
	int id = lastId + 1;
	int cnt = 0;
	for (int id = lastId + 1;cnt<maxArenas; id = (id + 1) % maxArenas, cnt++)
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

size_t ArenaControl::IdToAddress(int id)
{
	return ((size_t)id << arenaAddressShift) + virtualBase;
}

// Hack
// 1 = reset to GCHeap
// 2 = push new arena allocator
// 3 = push GCHeap
// 4 = pop
void _cdecl ArenaControl::SetAllocator(unsigned int type)
{
	const unsigned int MB = 1024 * 1024;
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
		arena = new Arena(Arena::Config(minBufferSize, maxBufferSize, IdToAddress(getId())));
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

void _cdecl ArenaControl::PushGC()
{
	if (ArenaControl::arenaStack == nullptr) return;
	::ArenaControl::Log("Push");
	arenaStack[arenaStackI++] = nullptr;
	if (arenaStackI > 7) { Log("arenastack overflow"); }
	arena = nullptr;
}

int Popcnt = 0;
void _cdecl ArenaControl::Pop()
{
	if (ArenaControl::arenaStack == nullptr) return;
	::ArenaControl::Log("Pop");
	Popcnt++;
	DeleteAllocator(arenaStack[--arenaStackI]);
	arena = nullptr;
	if (arenaStackI > 0) {
		arena = arenaStack[arenaStackI - 1];
	}
}

void* ArenaControl::GetArena()
{
	return arena;
}

void ArenaControl::DeleteAllocator(void* vallocator)
{
	Arena* allocator = static_cast<Arena*> (vallocator);
	if (allocator == nullptr) return;
	delete allocator;
}

inline
size_t ArenaControl::Align(size_t nbytes, int alignment)
{
	return (nbytes + alignment) & ~alignment;
}

void* ArenaControl::Allocate(size_t jsize)
{	
	if (arena == nullptr)
	{
		return nullptr;
	}
	size_t size = Align(jsize);
	return ((Arena*)arena)->Allocate(size);
}
int lcnt = 0;
void ArenaControl::Log(char *str, size_t n)
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


