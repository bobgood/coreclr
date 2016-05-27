// Hack
#pragma once

#include <vcruntime.h>
#include "common.h"
#include "..\vm\threads.h"


//#define VERIFYALLOC
#define ARENA_LOGGING

typedef short ArenaId;
typedef int BufferId;

class Arena;
class ArenaThread;
class ArenaStack;

////////////////////////////////////////////////////////
// ArenaStack
//
// A Stack<size_t>. This class is 
// part of every thread object.  This object holds
// the Stack of Allocators for each thread.
////////////////////////////////////////////////////////

class ArenaStack
{
	// Initialized with fixed stack depth
	static const int c_arenaStackDepth = 10;
	void *m_current = nullptr;
	void **m_stack = nullptr;
	size_t m_size = 0;
	size_t m_reserved = c_arenaStackDepth;

	void *m_fixedStack[c_arenaStackDepth];

public:
	ArenaStack()
	{
		m_stack = m_fixedStack;
		m_reserved = c_arenaStackDepth;
	}

	~ArenaStack()
	{
		if (m_stack != m_fixedStack) delete m_stack;
	}

	void *Current()
	{
		return m_current;
	}

	void Reset()
	{
		m_size = 0;
		m_current = nullptr;
	}

	int Size()
	{
		return (int)m_size;
	}


	void *operator[](size_t offset)
	{
		assert(offset >= 0 && offset < m_size);
		return m_stack[offset];
	}

	void Push(void *v)
	{

		m_current = v;
		if (m_size >= m_reserved)
		{
			m_reserved *= 2;
			void **stack2 = (void **)new size_t[m_reserved];
			
			for (size_t i = 0; i < m_size; i++) stack2[i] = m_stack[i];
			for (size_t i = m_size; i < m_reserved; i++) stack2[i] = 0;
			if (m_stack != m_fixedStack) delete m_stack;
			m_stack = stack2;
		}
		m_stack[m_size++] = v;
	}

	void *Pop()
	{
		DWORD written;
		m_size--;
		assert(m_size > 0);
		if (m_size == 0)
		{
			m_current = nullptr;		
		}
		else
		{
			m_current = m_stack[m_size - 1];
		}

		return m_stack[m_size];
	}
};

////////////////////////////////////////////////////////
// ArenaManager
//
// This class is the public interface to all Arena
// functionality (except ArenaStack is embedded in the thread object.
//
////////////////////////////////////////////////////////

class ArenaManager
{
public:
	// x64 process memory is 8192GB (8TB) (43 bits)
	// Each arena uses a fixed location in virtual memory, which are precalculated based on constants.
	// Each arena creates a set of buffers on demand, doubling size on each new buffer from
	// minBufferSize to maxBufferSize.
	static const int c_addressBits = 43;
	// The base address of arenas (to distinguish arenas from other memory)
	static const size_t c_arenaBaseAddress = 1ULL << (c_addressBits - 1); // half of virtual address space reserved for arenas

	static const size_t c_arenaBaseSize = (size_t)256 * 1024 * 1024 * 1024;  // 256GB max for arenas for now...
	static const size_t c_arenaRangeEnd = c_arenaBaseAddress + c_arenaBaseSize;

	static const size_t c_bufferReserveSize = 1024 * 1024;
	
	// guard page cannot be zero, because VirtualAlloc has difficulty
	// putting buffers immediately ajacent, and we need virtual buffers
	// to start on a known address boundary.
	static const size_t c_guardPageSize = 16 * 1024;
	static const size_t c_bufferSize = c_bufferReserveSize - c_guardPageSize;

	static const int c_maxArenas = 4096;
private:
	// Reservation system for all arenas.
	static unsigned long m_refCount[c_maxArenas];
	static void *m_arenaById[c_maxArenas];
#ifdef ARENA_LOGGING
	static HANDLE m_hFile;
	static int m_lcnt;
#endif

	static Arena *MakeArena();

	// The last arena ID allocated.  This is used to maximize the time between when an arena is destroyed, and
	// when the same virtual address space will be reused.
	static unsigned int lastId;

	// Deletes an Arena and releases all its memory.
	static void DeleteAllocator(void *);

	// Gets the next available Arena ID
	inline static int getId();

	// decrements the reference count, and releases the arena if zero
	static void DereferenceId(int id);

	// adds to the reference count
	static void ReferenceId(int id);

	// Gets the allocator at the top of the stack
	static void *ArenaManager::GetArena()
	{
		return GetArenaStack().Current();
	}
	
	static size_t Align(size_t nbytes)
	{
#if defined(BIT64)
		return (nbytes + 7) & ~7;
#else
		return (nbytes + 3) & ~3;
#endif
	}

	static void *AllocatorFromAddress(void *addr)
	{
		ArenaId id = GetArenaId(addr);
		if (id == -1) return nullptr;
		return m_arenaById[id];
	}

	static void RegisterForFinalization(Object *o, size_t size);

	static void MemCopy(void *idst, void *isrc, size_t len)
	{
#if defined(BIT64)
		size_t* dst = (size_t*)idst;
		size_t* src = (size_t*)isrc;
		int cnt = (int)(len >> 3);
		while (cnt--)*dst = *src;
#else
		unsigned* dst = (unsigned*)idst;
		unsigned* src = (unsigned*)isrc;
		int cnt = (int)(len >> 2);
		while (cnt--)*dst = *src;
#endif
	}

public:
	static void MemClear(void *idst, size_t len)
	{
#if defined(BIT64)
		size_t* dst = (size_t*)idst;
		int cnt = (int)(len >> 3);
		while (cnt--)*dst = 0;
#else
		unsigned *dst = (unsigned*)idst;
		int cnt = (int)(len >> 2);
		while (cnt--)*dst = 0;
#endif
	}

	// Initializes all Arena structures (call this once per process, before all other calls).
	static void InitArena();

	// This is the method that the user C# process calls to set the allocator state
	// 1 = reset to GCHeap
	// 2 = push new arena allocator
	// 3 = push GCHeap
	// 4 = pop
	// 1024+ push old arena
	static void SetAllocator(unsigned int type);

	// Gets the arenaID for the current arena in this thread,
	// returns -1, if no arena is the current allocator for this thread.
	static ArenaId GetArenaId();

	// Gets the arena ID given an address of an object, arena or arenathread
	static ArenaId GetArenaId(void *addr);

	// returns null if no arena allocator is active, otherwise returns
	// a pointer to an allocated buffer
	static void *Allocate(size_t jsize, uint32_t flags);

	// returns the address of the current ArenaThread object for this thread
	static void *Peek();

	// Returns a pointer to allocated memory from a specific arena
	static void *Allocate(ArenaThread *arena, size_t jsize);

	// Log method that writes to STD_OUTPUT
	static void Log(char *str, size_t n = 0, size_t n2 = 0, char *hdr = nullptr, size_t n3=0);

	// registers the address of an arena allocated object for
	// later verification
	static void RegisterAddress(void *addr)
	{
#ifdef VERIFYALLOC
		auto p = (size_t**)0x60000000000;
		*(*p)++ = (size_t)addr;
#else
		UNREFERENCED_PARAMETER(addr);
#endif
	}

#ifdef VERIFYALLOC
	// verifies the correctness of any registered object
	__declspec(noinline)
		static void VerifyAllArenaObjects();
#endif

	// Pushes nullptr onto this threads arena stack to signify
	// that no arena should be used for this thread 
	static void PushGC()
	{
		GetArenaStack().Push(nullptr);
	}

	// Pop's the state of the arena stack for this thread
	// to the state prior to the last push.
	static void Pop() {
		void *allocator = GetArenaStack().Pop();
		if (allocator != nullptr)
		{
			DereferenceId(GetArenaId(allocator));
		}
	}

	// Pushes nullptr onto a different threads arena stack to signify
	// that no arena should be used for this thread 
	static void PushGC(Thread *thread)
	{
		GetArenaStack(thread).Push(nullptr);
	}

	// Pop's the state of the arena stack for a specified thread
	// to the state prior to the last push.
	static void Pop(Thread *thread) {
		void *allocator = GetArenaStack(thread).Pop();
		if (allocator != nullptr)
		{
			DereferenceId(GetArenaId(allocator));
		}
	}

	// Clones the object with address src, and places
	// a pointer to the cloned object at target.
	static void ArenaMarshal(void *target, void *src);

	// True if the supplied pointer is within the arena
	// address space (the address space from 400'00000000 to 7ff'ffffffff)
	// values above this range (i.e. 7fff'00000000) are not arena, but rather code
	// and compiled data.
	static bool IsArenaAddress(void *p) {
		return (((size_t)p >> (ArenaManager::c_addressBits - 1)) == 1);
	}

	// True if p and q are both pointers within the same arena
	static bool IsSameArenaAddress(void *p, void *q); 

#ifdef VERIFYALLOC
	static void VerifyObject(Object *o, MethodTable *pMT = nullptr);
	static void VerifyClass(Object *o, MethodTable *pMT = nullptr);
	static void VerifyArray(Object *o, MethodTable *pMT = nullptr);
#endif

	// Gets the ArenaStack for the current thread.
	static ArenaStack &GetArenaStack();

	// Gets the ArenaStack for a specified thread.
	static ArenaStack &GetArenaStack(Thread *thread);

	// Creates a buffer in the arena virtual address space
	static void *CreateBuffer(ArenaId arenaId, size_t len = ArenaManager::c_bufferSize);

#if defined(BIT64)
	// Card byte shift is different on 64bit.
#define card_byte_shift     11
#else
#define card_byte_shift     10
#endif

#define card_byte(addr) (((size_t)(addr)) >> card_byte_shift)

	// to guarantee inline, this code is cloned from GCSample
	static void ErectWriteBarrier(Object ** dst, Object * ref)
	{
#if !defined(DACCESS_COMPILE)
		// if the dst is outside of the heap (unboxed value classes) then we
		//      simply exit
		if (((uint8_t*)dst < g_lowest_address) || ((uint8_t*)dst >= g_highest_address))
			return;

		if ((uint8_t*)ref >= g_ephemeral_low && (uint8_t*)ref < g_ephemeral_high)
		{
			// volatile is used here to prevent fetch of g_card_table from being reordered 
			// with g_lowest/highest_address check above. See comment in code:gc_heap::grow_brick_card_tables.
			uint8_t* pCardByte = (uint8_t *)*(volatile uint8_t **)(&g_card_table) + card_byte((uint8_t *)dst);
			if (*pCardByte != 0xFF)
				*pCardByte = 0xFF;
		}
#endif
	}

};

#define ISARENA(x) ::ArenaManager::IsArenaAddress(x)
#define ISSAMEARENA(x,y) ::ArenaManager::IsSameArenaAddress(x,y)

#define START_NOT_ARENA_SECTION ::ArenaManager::PushGC();
#define END_NOT_ARENA_SECTION ::ArenaManager::Pop();
