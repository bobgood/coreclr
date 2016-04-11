// Hack
#include "arena.h"
#include "ArenaAllocator.h"
#include "vector.h"
thread_local int ArenaControl::arenaStackI = 0;
thread_local void** ArenaControl::arenaStack = nullptr;
size_t ArenaControl::virtualBase;
typedef sfl::NonValidatingArena Arena;

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
	Assert(idInUse[id]);
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

void ArenaControl::Assert(bool n)
{
	if (n) return;
	throw("BOB Assert");
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
	Assert(type >= 1 && type <= 4);
	switch (type)
	{
	case 1:
		for (int i = 0; i < arenaStackI; i++) DeleteAllocator(arenaStack[i]);
		arenaStackI = 0;
		arenaStack[arenaStackI++] = nullptr;
		break;
	case 2:
		arenaStack[arenaStackI++] = new sfl::NonValidatingArena(Arena::Config(minBufferSize, maxBufferSize, IdToAddress(getId())));
		break;
	case 3:
		arenaStack[arenaStackI++] = nullptr;
		break;
	case 4:
		DeleteAllocator(arenaStack[--arenaStackI]);
		if (arenaStack < 0) arenaStackI = 0;
		break;
	}
}

void Assert(bool ok)
{
	if (ok) return;
	
}

void ArenaControl::DeleteAllocator(void* vallocator)
{
	Arena* allocator = static_cast<Arena*> (vallocator);
	if (allocator == nullptr) return;
	delete allocator;
}
