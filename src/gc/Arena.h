// Hack
#pragma once
#include <vcruntime.h>

class ArenaControl
{

	static thread_local int arenaStackI;
	// Allocator type not enforced becuase arena library is incompatible with CLR
	static thread_local void** arenaStack;

	static size_t virtualBase;

private:
	static void DeleteAllocator(void*);
public:
	static void InitArena();
	static void SetAllocator(unsigned int type);
	static bool try_allocate_more_space(void* acontext, size_t size,
		int gen_number);
};
