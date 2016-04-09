#pragma once
// Hack
#ifdef USAGE_EXAMPLES

template <typename Unused>
inline void ArenaUsageExamples()
{

	//
	// Basics
	//

	// Thread safe arena allocator which keeps ref count of allocations
	sfl::Arena arena;

	void* ptr = arena.Allocate(100 * sizeof(GUID));

	// ...

	arena.Deallocate(ptr);

	// ...

	// Reset arena to its initial state. All memory allocated from the arena
	// is considered released after this point, and allocator asserts that
	// number of allocations and deallocations match.
	arena.Reset();

	//
	// Configuration settings
	//
	// Arena allocator can be configured with following parameters:
	//
	//    minBuffer     - initial, preallocated arena buffer, by default 64 KB
	//    maxBuffer     - max size arena can grow to, by default (minBuffer << 5)
	//    maxArenaAlloc - max size of allocation that will be satisfied from
	//                    the arena, by default (minBuffer / 2). 
	//                    Larger allocation will be sent to overflow allocator.

	// The defaults settings can be changed by passing Arena::Config to the
	// constructor. 

	// Arena with 128 KB initial buffer size
	sfl::Arena arean128KB(128 * 1024);

	// Arena with a single, preallocated 4 MB buffer that will not grow
	const uint32_t _4MB = 4 * 1024 * 1024;

	sfl::Arena arena4MB(sfl::Arena::Config(_4MB, _4MB));

	//
	// Predefined flavors of arena allocator 
	//

	// Thread safe, non-validating arena allocator. Deallocate is a noop and thus optional.
	typedef sfl::NonValidatingArena Arena;

	Arena arena1;

	arena1.Allocate(100);
	arena1.Reset();

	// Allocator for use in arena-per-thread model. Slightly faster than the thread safe version.
	sfl::SingleThreadArena arena2;

	// Minimal arena allocator: no validation (Deallocate is noop) and no thread safety.
	sfl::NonValidatingSingleThreadArena arena3;

	//
	// Customization via policies
	//

	class NoAllocator
	{
	public:
		void* Allocate(size_t)
		{
			throw std::bad_alloc();
		}

		void Deallocate(void*)
		{}

		void Reset()
		{}
	};

	// Arena allocator that will not fall back to heap. 
	// Allocations larger than 1/2 of the minimal arena buffer, and all allocations
	// after arena buffer(s) are exhausted, will fail with std::bad_alloc exception.
	typedef sfl::ArenaAllocator<sfl::policy::ThreadSafe, sfl::policy::NoValidation, NoAllocator>
		NoOverflowArena;

	NoOverflowArena arena4;

	// For usage as STL allocator with standard library containers see StlAllocator.h
	// For usage with operator new/delete overloading see NewOverload.h

}

#endif // USAGE_EXAMPLES


#include "lock.h"
#include "vector.h"
//#include <mutex>
#include "HeapAllocator.h"
#include "AllocatorPolicy.h"
#include "debugmacros.h"


namespace sfl
{


#pragma warning(push)
	// warning C4355: 'this' : used in base member initializer list
#pragma warning(disable: 4355)
	// warning C4324: 'sfl::ArenaAllocator<ConcurrencyPolicy,AllocationPolicy>' : structure was padded due to __declspec(align())
#pragma warning(disable: 4324)

	// ArenaAllocator
	template <typename ConcurrencyPolicy = policy::ThreadSafe,
		typename AllocationPolicy = policy::RefcountValidation<ConcurrencyPolicy>,
		typename OverflowAllocator = HeapAllocator>
	class ArenaAllocator
		: public AllocationPolicy

	{
	public:
		// Standard Library allocator adapter


		// Configuration settings
		struct Config
		{
			Config(uint32_t minBuffer = 64 * 1024)
				: minBuffer(minBuffer),
				maxBuffer(minBuffer << 5),
				maxArenaAlloc(minBuffer / 2)
			{}


			Config(uint32_t minBuffer, uint32_t maxBuffer)
				: minBuffer(minBuffer),
				maxBuffer(maxBuffer),
				maxArenaAlloc(minBuffer / 2)
			{}


			Config(uint32_t minBuffer, uint32_t maxBuffer, uint32_t maxArenaAlloc)
				: minBuffer(minBuffer),
				maxBuffer(maxBuffer),
				maxArenaAlloc(maxArenaAlloc)
			{}


			// Initial size of arena buffer, preallocated during construction
			uint32_t minBuffer;

			// Maximum amount of memory to be used for arena buffer
			uint32_t maxBuffer;

			// Maximum size of allocation that will be satisfied from arena.
			// Requests larger than this threshold will be sent to overflow allocator.
			uint32_t maxArenaAlloc;
		};


		const Config m_config;


		// c/dtor
		ArenaAllocator(const Config& config = Config())
			: m_config(config)
		{
			// Calculate maximum number of exponentially increasing arena buffers
			// we can allocate and still stay within specified max buffer limit.
			int buffers = 1;

			for (uint32_t size = config.minBuffer, total = config.maxBuffer;
			size < total; ++buffers)
			{
				total -= size;
				size <<= 1;
			}

			// Reserve space in m_arenaBuffers vector
			m_arenaBuffers.reserve(buffers);

			AddBuffer();
		}


		~ArenaAllocator()
		{
			FreeBuffers();
			AllocationPolicy::Reset();
		}


		// Free memory and recycle the arena so it can be reused
		void Reset()
		{
			// Free all but the first arena buffer
			FreeBuffers(1);

			// Reset m_nextBlock to the beginning of the arena buffer
			m_nextBlock.bufferIndex = 0;
			m_nextBlock.bufferOffset = initialOffset;

#ifdef _DEBUG
			memset(m_arenaBuffers[0], 0xBA, m_config.minBuffer);
#endif

			AllocationPolicy::Reset();
		}


		// Allocate a block of memory of specified size from an arena buffer,
		// or from overflow allocator if the size is greater than maxArenaAlloc.
		void* Allocate(size_t size)
		{
			AllocationPolicy::Allocate(size);

			if (size > m_config.maxArenaAlloc)
			{
				return AllocateOverflow(size);
			}

			uint32_t size32 = static_cast<uint32_t>(size);

			if (void* p = AllocateFastPath(size32))
			{
				return p;
			}

			return AllocateSlowPath(size32);
		}


		// "Free" memory allocated by the allocator. Any memory allocated by arena
		// allocator is actually not freed until the arena is destroyed or Reset.
		// The exact semantics of Deallocate depend on AllocationPolicy. 
		void Deallocate(void* p)
		{
			AllocationPolicy::Deallocate(p);
		}


		bool operator==(const ArenaAllocator& rhs) const
		{
			return (this == &rhs);
		}


	protected:
		// Try to allocate requested block from current arena buffer w/o taking any locks
		void* AllocateFastPath(uint32_t size)
		{
			Block nextBlock, myBlock;

			for (;;)
			{
				// Atomic copy of next available block
				nextBlock = myBlock = m_nextBlock;

				// Pad if necessary to keep the next block aligned
				const uint32_t alignment = std::alignment_of<int64_t>::value;
				size = (size + alignment - 1) & (0 - alignment);

				// Update the next block to point past this allocation
				nextBlock.bufferOffset += size;

				if (nextBlock.bufferOffset > (m_config.minBuffer << nextBlock.bufferIndex))
				{
					// Current arena buffer doesn't have enough space for this allocation.
					// We have to fall back to slow path.
					return NULL;
				}

				if (ConcurrencyPolicy::CompareExchange(m_nextBlock, nextBlock, myBlock))
				{
					// It is safe to read from m_arenaBuffers vector w/o a lock because
					// we preallocate it in the constructor and never allow it to grow
					// or shrink.
					return static_cast<uint8_t*>(m_arenaBuffers[myBlock.bufferIndex]) + myBlock.bufferOffset;
				}

				// If CompareExchange failed it means that another thread has
				// allocated a block from the current arena buffer or added a new
				// arena buffer. We need to retry.
			}
		}


		// Try to add a new arena buffer and allocate a block from it, or if we
		// already have maximum number or arena buffers then fall back to overflow
		// allocator.
		void* AllocateSlowPath(uint32_t size)
		{
			std::lock_guard<typename ConcurrencyPolicy::Lock> guard(m_arenaBuffersLock);

			for (;;)
			{
				// Although we got here after failing on the fast path, it is possible
				// that before we entered the critical section, another thread has 
				// already added a new arena buffer. To avoid wasting memory we try
				// to allocate from the current buffer first.
				if (void* p = AllocateFastPath(size))
				{
					return p;
				}


				if (m_arenaBuffers.size() == m_arenaBuffers.capacity())
				{
					// We've reached limit of arena buffers; fall back to overflow
					// allocator.
					return AllocateOverflow(size);
				}


				AddBuffer();
				// Even though we are inside a critical section, we can't assume
				// that we will be able to get a block from the buffer we've just
				// added. We must continue in the loop, possibly adding more buffers,
				// until we either acquire a block or reach limit of arena buffers.
			}
		}


		// Allocate using the overflow allocator
		void* AllocateOverflow(size_t size)
		{
			void* p = m_overflowAllocator.Allocate(size);

			std::lock_guard<typename ConcurrencyPolicy::Lock> guard(m_overflowBuffersLock);

			m_overflowBuffers.push_back(p);

			return p;
		}


		// Add a new arena buffer
		void AddBuffer()
		{
			// m_arenaBuffers vector must never grow
			assert(m_arenaBuffers.size() < m_arenaBuffers.capacity());

			// Set the next block to the beginning of the buffer we are about to add 
			Block nextBlock;

			nextBlock.bufferIndex = static_cast<uint32_t>(m_arenaBuffers.size());
			nextBlock.bufferOffset = initialOffset;

			// Increase the size of the arena buffers exponentially  
			m_arenaBuffers.push_back(operator new(m_config.minBuffer << nextBlock.bufferIndex));

			// The fast path assumes that arena buffer pointed to by m_nextBlock is
			// already present in m_arenaBuffers vector. MemoryBarrier makes sure
			// that compiler/CPU doesn't reorder the push_back above and update of
			// m_nextBlock below.
			ConcurrencyPolicy::MemoryBarrier();

			// Atomic update of the next block
			m_nextBlock = nextBlock;
		}


		void FreeBuffers(size_t bufferToKeep = 0)
		{
			// We don't bother with thread safety because reseting or destroying arena
			// while it is in use is a fatal bug regardless of concurrency.

			// Free arena buffers
			assert(!m_arenaBuffers.empty());
			for (size_t i = bufferToKeep; i < m_arenaBuffers.size(); i++)

			{
				delete (Arena*)m_arenaBuffers[i];
			}

			m_arenaBuffers.resize(bufferToKeep);

			// Free the overflow buffers

			for (size_t i = bufferToKeep; i < m_overflowBuffers.size(); i++)
			{
				m_overflowAllocator.Deallocate(m_overflowBuffers[i]);
			}

			m_overflowBuffers.resize(0);
			m_overflowAllocator.Reset();
		}


	protected:
		// Block objects must be 64 bit aligned to guarantee that assignment is atomic
		__declspec (align(8))
		struct Block
		{
			Block()
			{}

			Block(const volatile Block& rhs)
			{
				*this = rhs;
			}

			volatile Block& operator=(const volatile Block& rhs) volatile
			{
				// Atomic assignment of Block object on 64 bit platforms
				*reinterpret_cast<volatile uint64_t*>(this) = reinterpret_cast<const volatile uint64_t&>(rhs);
				return *this;
			}

			// Index of arena buffer the block is in
			uint32_t bufferIndex;

			// Block's offset within the buffer
			uint32_t bufferOffset;
		};


		// If user tries to shoot himself in the foot and calls global operator
		// delete() or free() on an arena allocated object, offsetting first
		// allocation by a few bytes will make sure he doesn't miss. 
		static const uint32_t initialOffset =
#ifdef DEBUG
			sizeof(void*);
#else
			0;
#endif

		// Descriptor of the next available block of memory
		volatile Block                      m_nextBlock;

		// List of arena buffers and lock for updates
		vector<void*>                  m_arenaBuffers;
		typename ConcurrencyPolicy::Lock    m_arenaBuffersLock;

		// List of overflow buffers and lock for updates
		vector<void*>                  m_overflowBuffers;
		typename ConcurrencyPolicy::Lock    m_overflowBuffersLock;

		// Overflow allocator
		OverflowAllocator                   m_overflowAllocator;
	};


#pragma warning(pop)


	typedef ArenaAllocator<>
		Arena;

	typedef ArenaAllocator<policy::ThreadSafe, policy::NoValidation>
		NonValidatingArena;

	typedef ArenaAllocator<policy::SingleThread>
		SingleThreadArena;

	typedef ArenaAllocator<policy::SingleThread, policy::NoValidation>
		NonValidatingSingleThreadArena;

}; // namespace sfl
