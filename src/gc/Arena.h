// Hack
#pragma once
#include <vcruntime.h>

class ArenaControl
{
	// x64 process memory is 8192GB (8TB) (43 bits)
	// Each arena uses a fixed location in virtual memory, which are precalculated based on constants.
	// Each arena creates a set of buffers on demand, doubling size on each new buffer from
	// minBufferSize to maxBufferSize.
	
	// number of arenas to support (and provide address spaces for.)
	static const int maxArenas = 1024;

	// The amount of address space allocated per arena (1<<32 == 4GB)
	static const int arenaAddressShift = 32;  // at most 4GB per arena total

	// The base address of arenas (to distinguish arenas from other memory)
	static const size_t arenaBaseRequest = 1ULL<<42; // half of virtual address space reserved for arenas

	// Minimum and maximum size of buffers allocated to arenas (each new buffer is twice the size of the prior)
	static const size_t minBufferSize = 1ULL << 24;  //(16MB) min per arena
	static const size_t maxBufferSize = (1ULL << (arenaAddressShift - 1));

	// Ensure all arena buffers fit in X64 virtual process memory.
	static_assert (((size_t)maxArenas << arenaAddressShift) + arenaBaseRequest <= (1ULL << 43), "arenas use too much memory");

	// Reservation system for all arenas.
	static bool idInUse[maxArenas];

	// Each thread holds a stack of arenas (which are in heap memory, not arena memory)
	static thread_local int arenaStackI;
	// Allocator type not enforced becuase arena library is incompatible with CLR
	static thread_local void** arenaStack;

	// Should be the same as arenaBaseRequest.  This is the actual location in virtual memory for all arenas.
	static size_t virtualBase;

	// Implements Assert method that does not seem to be available
	static void Assert(bool n);

	// The last arena ID allocated.  This is used to maximize the time between when an arena is destroyed, and
	// when the same virtual address space will be reused.
	static unsigned int lastId;

	// Converts an arena ID into a base address in virtual address space.
	static size_t IdToAddress(int id);

private:
	// Deletes an Arena and releases all its memory.
	static void DeleteAllocator(void*);

	// Gets the next available Arena ID
	static int getId();

	// Releases an Arena ID
	static void ReleaseId(int id);

public:
	// Initializes all Arena structures (call this once per process, before all other calls).
	static void InitArena();

	// This is the method that the user C# process calls to set the allocator state
	static void SetAllocator(unsigned int type);
};
