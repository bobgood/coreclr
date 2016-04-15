#pragma once
// Hack
//
// This library is a version of ServiceFoundation.Library, with the following modifications:
// 1. removed Policy classes - we only want thread safe nonvalidating arenas here.
// 2. Changed locks to use CLR locks
// 3. replaced all std and boost classes.
// 4. created a lightweight replacement for vector
// 5. modified all buffer allocations to use VirtualAlloc

#include "lock.h"
#include "vector.h"
#include "HeapAllocator.h"
#include "debugmacros.h"

namespace sfl
{
	////////////////////////////////
	// copied from std::type_traits
	////////////////////////////////

	template<class _Ty,
		_Ty _Val>
	struct integral_constant
	{	// convenient template for integral constant types
		static constexpr _Ty value = _Val;

		typedef _Ty value_type;
		typedef integral_constant<_Ty, _Val> type;

		constexpr operator value_type() const noexcept
		{	// return stored value
			return (value);
		}

		constexpr value_type operator()() const noexcept
		{	// return stored value
			return (value);
		}
	};

	typedef integral_constant<bool, true> true_type;
	typedef integral_constant<bool, false> false_type;

	template<class _Ty>
	struct alignment_of
		: integral_constant<size_t, alignof(_Ty)>
	{	// determine alignment of _Ty
	};

	////////////////////////////////////
	// end of copy
	////////////////////////////////////

#pragma warning(push)
	// warning C4355: 'this' : used in base member initializer list
#pragma warning(disable: 4355)
	// warning C4324: 'sfl::ArenaAllocator<ConcurrencyPolicy,AllocationPolicy>' : structure was padded due to __declspec(align())
#pragma warning(disable: 4324)

	// ArenaAllocator
	class ArenaAllocator
	{
	public:
		// Standard Library allocator adapter


		// Configuration settings
		struct Config
		{
			Config(uint32_t minBuffer = 64 * 1024)
				: minBuffer(minBuffer),
				maxBuffer(minBuffer << 5),
				maxArenaAlloc(minBuffer / 2),
				addr(0)
			{}


			Config(uint32_t minBuffer, uint32_t maxBuffer)
				: minBuffer(minBuffer),
				maxBuffer(maxBuffer),
				maxArenaAlloc(minBuffer / 2),
				addr(0)
			{}


			Config(uint32_t minBuffer, uint32_t maxBuffer, uint64_t iaddr)
				: minBuffer(minBuffer),
				maxBuffer(maxBuffer),
				maxArenaAlloc(minBuffer / 2),
				addr(iaddr)
			{}

			Config(uint32_t minBuffer, uint32_t maxBuffer, uint64_t iaddr, uint32_t maxArenaAlloc)
				: minBuffer(minBuffer),
				maxBuffer(maxBuffer),
				maxArenaAlloc(maxArenaAlloc),
				addr(iaddr)
			{}


			// Initial size of arena buffer, preallocated during construction
			uint32_t minBuffer;

			// Maximum amount of memory to be used for arena buffer
			uint32_t maxBuffer;

			// If non-zero, a VirtualAlloc is used for the buffers at a location starting at Addr
			uint64_t addr;

			// Maximum size of allocation that will be satisfied from arena.
			// Requests larger than this threshold will be sent to overflow allocator.
			uint32_t maxArenaAlloc;
		};

		/////////////////////////////////////////////////////////////////////////////
		// WARNING WARNING
		// if these offsets change, they need to be also changed in Asmconstants.tmp
#define ArenaAllocator_Config_minBuffer_Offset 0
#define ArenaAllocator_Config_maxBuffer_Offset 4
#define ArenaAllocator_Config_addr_Offset 8
#define ArenaAllocator_Config_maxArenaAlloc_Offset 0x10
		/////////////////////////////////////////////////////////////////////////////

		static_assert(offsetof(Config, minBuffer) == ArenaAllocator_Config_minBuffer_Offset, "minBuffer offset is not 0");
		static_assert(offsetof(Config, maxBuffer) == ArenaAllocator_Config_maxBuffer_Offset, "maxBuffer offset is not 4");
		static_assert(offsetof(Config, addr) == ArenaAllocator_Config_addr_Offset, "minBuffer offset is not 8");
		static_assert(offsetof(Config, maxArenaAlloc) == ArenaAllocator_Config_maxArenaAlloc_Offset, "maxBuffer offset is not 0x10");



		// c/dtor
		ArenaAllocator(const Config& config = Config())
			: m_config(config),
			m_virtualAddress(config.addr)
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

		}


		// Allocate a block of memory of specified size from an arena buffer,
		// or from overflow allocator if the size is greater than maxArenaAlloc.
		void* Allocate(size_t size)
		{
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
				const uint32_t alignment = sfl::alignment_of<int64_t>::value;
				size = (size + alignment - 1) & (0 - alignment);

				// Update the next block to point past this allocation
				nextBlock.bufferOffset += size;

				if (nextBlock.bufferOffset > (m_config.minBuffer << nextBlock.bufferIndex))
				{
					// Current arena buffer doesn't have enough space for this allocation.
					// We have to fall back to slow path.
					return NULL;
				}

				if (CompareExchange(m_nextBlock, nextBlock, myBlock))
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
			sfl::lock_guard<sfl::critical_section> guard(m_arenaBuffersLock);

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

			void* p = AllocateBuffer(size);

			sfl::lock_guard<sfl::critical_section> guard(m_arenaBuffersLock);

			m_overflowBuffers.push_back(p);
			m_overflowBufferLengths.push_back(size);

			return p;
		}

		template<typename T>
		static bool CompareExchange(volatile T& dest, const T& new_value, const T& old_value,
			typename std::enable_if<sizeof(T) == sizeof(int64_t)>::type* = 0)
		{
			return reinterpret_cast<const int64_t&>(old_value) == InterlockedCompareExchange64(
				reinterpret_cast<volatile int64_t*>(&dest),
				reinterpret_cast<const int64_t&>(new_value),
				reinterpret_cast<const int64_t&>(old_value));
		}

		LPVOID AllocateBuffer(size_t size)
		{
			if (m_virtualAddress > 0)
			{
				size_t requestAddress;
				for (;;)
				{
					requestAddress = m_virtualAddress;
					auto nextAddress = requestAddress + size;
					if (CompareExchange(m_virtualAddress, nextAddress, requestAddress))
					{
						break;
					}
				}

				LPVOID addr = ClrVirtualAlloc((LPVOID)requestAddress, size, MEM_COMMIT, PAGE_READWRITE);		
				if ((size_t)addr != requestAddress)
				{
					if (addr == 0)
					{
						int err = GetLastError();
						throw "ClrVirtualAlloc error";
					}
					else {
						throw "ClrVirtualAlloc memory collision";
					}
				}

				return addr;
			}
			else
			{
				// Increase the size of the arena buffers exponentially  
				return operator new(size);
			}
		}

		void DeallocateBuffer(LPVOID addr, size_t size)
		{
			if (m_virtualAddress > 0)
			{
				ClrVirtualFree(addr, size, MEM_RELEASE);
			}
			else
			{
				delete addr;
			}
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

			m_arenaBuffers.push_back(AllocateBuffer(m_config.minBuffer << nextBlock.bufferIndex));

			// The fast path assumes that arena buffer pointed to by m_nextBlock is
			// already present in m_arenaBuffers vector. MemoryBarrier makes sure
			// that compiler/CPU doesn't reorder the push_back above and update of
			// m_nextBlock below.
			::MemoryBarrier();

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
				size_t size = m_config.minBuffer << i;
				DeallocateBuffer(m_arenaBuffers[i], size);
			}

			m_arenaBuffers.resize(bufferToKeep);

			// Free the overflow buffers

			for (size_t i = bufferToKeep; i < m_overflowBuffers.size(); i++)
			{
				DeallocateBuffer(m_overflowBuffers[i], m_overflowBufferLengths[i]);
			}

			m_overflowBuffers.resize(0);
			m_overflowBufferLengths.resize(0);
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
/////////////////////////////////////////////////////////////////////////////
// WARNING WARNING
// if these offsets change, they need to be also changed in Asmconstants.inc
#define ArenaAllocator_Block_bufferIndex_Offset 0
#define ArenaAllocator_Block_bufferOffset_Offset 4
/////////////////////////////////////////////////////////////////////////////

		static_assert(offsetof(Block, bufferIndex) == ArenaAllocator_Block_bufferIndex_Offset, "bufferIndex offset is not 0");
		static_assert(offsetof(Block, bufferOffset) == ArenaAllocator_Block_bufferOffset_Offset, "bufferOffset offset is not 4");
		// If user tries to shoot himself in the foot and calls global operator
		// delete() or free() on an arena allocated object, offsetting first
		// allocation by a few bytes will make sure he doesn't miss. 
		static const uint32_t initialOffset =
#ifdef DEBUG
			sizeof(void*);
#else
			0;
#endif

		// the following fields are public only for MASM access
		public:

		// Descriptor of the next available block of memory
		volatile Block                      m_nextBlock;

		const Config m_config;

		// List of arena buffers and lock for updates
		vector<void*>                  m_arenaBuffers;
		sfl::critical_section    m_arenaBuffersLock;

		// List of overflow buffers
		vector<void*>                  m_overflowBuffers;

		// List of overflow bufferlengths
		vector<size_t>                  m_overflowBufferLengths;


		volatile size_t m_virtualAddress;



	};

#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// WARNING WARNING
// if these offsets change, they need to be also changed in Asmconstants.inc
#define ArenaAllocator_m_nextBlock_Offset 0
#define ArenaAllocator_m_config_Offset 8
#define ArenaAllocator_m_arenaBuffers 0x20
/////////////////////////////////////////////////////////////////////////////
#define offsetofv(s,m)   (size_t)( (ptrdiff_t)&reinterpret_cast<volatile char&>((((s *)0)->m)) )
	static_assert(offsetofv(ArenaAllocator, m_nextBlock) == ArenaAllocator_m_nextBlock_Offset, "m_nextBlock offset is not 0");
	static_assert(offsetof(ArenaAllocator, m_config) == ArenaAllocator_m_config_Offset, "m_nextBlock offset is not 8");
	static_assert(offsetof(ArenaAllocator, m_arenaBuffers) == ArenaAllocator_m_arenaBuffers, "m_nextBlock offset is not 0x20");


}; // namespace sfl
