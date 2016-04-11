// Hack
#pragma once
#include <vcruntime.h>

class ArenaControl
{
	// x64 process memory is 8192GB (8TB) (43 bits)
	static const int maxArenas = 1024;
	static const int arenaAddressShift = 32;  // at most 4GB per arena
	static const size_t arenaBaseRequest = 1ULL<<42; // half of virtual address space reserved for arenas
	static bool idInUse[maxArenas];
	static thread_local int arenaStackI;
	// Allocator type not enforced becuase arena library is incompatible with CLR
	static thread_local void** arenaStack;

	static size_t virtualBase;
	static void Assert(bool n);
	static unsigned int lastId;
	static size_t IdToAddress(int id);

private:
	static void DeleteAllocator(void*);
	static int getId();
	static void ReleaseId(int id);

public:
	static void InitArena();
	static void SetAllocator(unsigned int type);
	static bool try_allocate_more_space(void* acontext, size_t size,
		int gen_number);
};
