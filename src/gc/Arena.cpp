// Hack
#include "arena.h"
#include "ArenaAllocator.h"

thread_local int ArenaControl::arenaStackI = 0;
thread_local void** ArenaControl::arenaStack = nullptr;
size_t ArenaControl::virtualBase;
typedef sfl::NonValidatingArena Arena;

void ArenaControl::InitArena()
{
	virtualBase = 1ULL << 62;
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
	switch (type)
	{
	case 1:
		for (int i = 0; i < arenaStackI; i++) DeleteAllocator(arenaStack[i]);
		arenaStackI = 0;
		arenaStack[arenaStackI++] = nullptr;
		break;
	case 2:

		arenaStack[arenaStackI++] = new sfl::NonValidatingArena(Arena::Config(20 * MB, 1000 * MB));
		break;
	case 3:
		arenaStack[arenaStackI++] = nullptr;
		break;
	case 4:
		DeleteAllocator(arenaStack[--arenaStackI]);
		if (arenaStack < 0) arenaStackI = 0;
		break;
	default:
		throw 0;

	}
}

bool ArenaControl::try_allocate_more_space(void* acontext, size_t size,
	int gen_number)
{
	return false;
}

void ArenaControl::DeleteAllocator(void* vallocator)
{
	Arena* allocator = static_cast<Arena*> (vallocator);
	if (allocator == nullptr) return;
	delete allocator;
}
