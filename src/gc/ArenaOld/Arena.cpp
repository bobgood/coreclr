
#include "../common.h"
#include "arena.h"
#include "../object.h"
#include <assert.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include "class.h"

#define LOGGING

TypeHandle LoadExactFieldType(FieldDesc *pFD, MethodTable *pEnclosingMT, AppDomain *pDomain);

// lastId keeps track of the last id used to create an arena.  
// the next one allocated will try at lastId+1, and keep trying
// until an empty one is found.  this means that ids will not be
// reused immediately, which means that a buffer becomes unaccessable
// for as long as possible after it is free.

unsigned int ArenaManager::lastId = 0;
void* ArenaManager::arenaById[maxArenas];
unsigned long ArenaManager::refCount[maxArenas];
HANDLE ArenaManager::hFile = 0;

////////////////////////////////////////////////////////
// ArenaVector
//
// implements a subset of std::vector
////////////////////////////////////////////////////////


template<class T>
class ArenaVector
{
	static const int fixedsize = 10;
public:
	// array is set to nullptr when fixed array is used, in order to support move semantics before much data
	// is added, when move semantics would not update the pointer.
	T* array;
	// Prebuild space into the class so that the vector can be used before there is an allocator ready
	T fixed[fixedsize];
	size_t isize;
	size_t reserved;

	// arena is stored as a void* becuase vector both needs to use arena, and is a component of arena.
	// both classes are templated, so vector cannot use the arena class within the .h file, and the method
public:
	ArenaVector()

	{
		reserved = fixedsize;
		array = fixed;
		isize = 0;
	}

	~ArenaVector()
	{
		if (array != fixed) delete array;
	}

	void reserve(size_t n)
	{
		if (reserved > n) return;  // never shrink when arenas are involved
		T* arr2 = new T[n];
		memset(arr2, 0, sizeof(T)*n);
		for (int i = 0; i < reserved; i++)
		{
			arr2[i] = array[i];
		}

		reserved = n;

		array = arr2;
	}

	void resize(size_t n)
	{
		if (n > reserved) reserve(n * 3 / 2);
		isize = n;
	}

	void Reset()
	{
		resize(0);
	}

	bool empty()
	{
		return isize == 0;
	}

	size_t size()
	{
		return isize;
	}

	size_t capacity()
	{
		return reserved;
	}

	void push_back(T v)
	{
		if (isize >= reserved)
		{
			reserve(reserved < 10 ? 10 : reserved * 3 / 2);
		}
		array[isize++] = v;
	}

	T pop_front()
	{
		assert(!empty());
		T ret = array[0];
		for (int i = 1; i < isize; i++)
		{
			array[i - 1] = array[i];
		}

		isize--;
		return ret;
	}


	void delete_repeat(T& ref)
	{
		for (int i = 0; i < isize; i++)
		{
			T& src = array[i];
			if (src == ref)
			{
				for (int j = i + 1; j < isize; j++)
				{
					array[j - 1] = array[j];
				}
				isize--;
				i--;
			}
		}
	}

	T& operator[](size_t n)
	{
		assert(n < isize);
		return array[n];
	}

	static Test()
	{
		ArenaVector<int> n;
		for (int i = 0; i < 100; i++)
		{
			n.push_back(i);
		}

		for (int i = 0; i < 100; i++)
		{
			n.push_back(i);
		}

		for (int i = 0; i < 100; i++)
		{
			n.push_back(i);
		}

		if (n.size() != 300) throw 0;
		for (int i = 0; i < 300; i++)
		{
			if (n[i] != i % 100) throw 0;
		}

		for (int i = 0; i < 100; i++)
		{
			if (n.empty()) throw 0;
			int k = n.pop_front();
			if (k != i) throw 0;
			n.delete_repeat(i);
		}

		if (!n.empty())
		{
			int k = n.pop_front();
			throw(0);
		}

	}
};


////////////////////////////////////////////////////////
// ArenaSet
//
// implements a small variant of std::set<size_t>
////////////////////////////////////////////////////////

#ifdef VERIFYALLOC

class ArenaSet
{
	size_t* table;
	int slotCount;
	long lock;
	static const int tries = 5;
public:
	ArenaSet(int slots = 97)
	{
		lock = 0;
		slotCount = slots;
		table = new size_t[slots];
		for (int i = 0; i < slots; i++) table[i] = 0;
	}

	void add(void* t)
	{
		add((size_t)t);
	}

	__declspec(noinline)
		void add(size_t n)
	{
		assert(n != 0);
	retry:
		SpinLock(lock);
		int key = (n >> 3) % slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < tries; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i] == 0 || table[i] == n)
			{
				table[i] = n;
				SpinUnlock(lock);
				return;
			}
		}

		int s2 = slotCount * 2 - 1;
		size_t*table2 = new size_t[s2];
		for (int i = 0; i < s2; i++) table2[i] = 0;
		for (int i = 0; i < slotCount; i++)
		{
			size_t m = table[i];
			if (m != 0)
			{
				int key = (m >> 3) % s2;
				int cnt = 0;
				for (int i = key; cnt < tries; i = (i == s2 - 1) ? 0 : i + 1)
				{
					if (table2[i] == 0 || table2[i] == m)
					{
						table2[i] = m;
						break;
					}
				}

			}
		}

		delete table;
		table = table2;
		slotCount = s2;
		SpinUnlock(lock);
		goto retry;
	}

	void clear()
	{
		SpinLock(lock);
		for (int i = 0; i < slotCount; i++) table[i] = 0;
		SpinUnlock(lock);
	}

	bool contains(void* n)
	{
		return contains((size_t)n);
	}

	__declspec(noinline)
		bool contains(size_t n)
	{
		SpinLock(lock);
		int key = (n >> 3) % slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < tries; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i] == 0 || table[i] == n)
			{

				bool r = table[i] == n;
				SpinUnlock(lock);
				return r;
			}
		}
		SpinUnlock(lock);
		return false;
	}
private:
	void SpinLock(long& lock)
	{
		while (0 != InterlockedCompareExchange(&lock, 1, 0));
	}

	void SpinUnlock(long& lock)
	{
		while (1 != InterlockedCompareExchange(&lock, 0, 1));
	}

public:
	static void Test()
	{
		ArenaSet s;
		for (size_t i = 1; i < 1000; i++)
		{
			size_t k = i * 0x33008;
			s.add(k);
		}
		for (size_t i = 1; i < 1000; i++)
		{
			size_t k = i * 0x33008;
			if (!s.contains(k)) throw 0;
			if (s.contains(k - 1)) throw 0;
		}
	}
};

ArenaSet verifiedObjects;

#endif // VERIFYALLOC

////////////////////////////////////////////////////////
// ArenaHashTable
//
// 
////////////////////////////////////////////////////////


void* RunAllocator(void* allocator, size_t size);

struct KVP
{
	size_t m_key;
	size_t m_value;
};

class ArenaHashtable
{
	KVP* m_table;
	int m_slotCount;
	long m_lock;
	void *m_arena;
	static const int c_tries = 5;
public:
	ArenaHashtable(int slots = 100)
	{
		m_lock = 0;
		m_slotCount = slots;
		m_arena = 0;
		m_table = nullptr;
	}
	ArenaHashtable(void* arena, int slots = 100)
	{
		m_lock = 0;
		m_slotCount = slots;
		m_arena = arena;
		m_table = nullptr;
	}

	void LazyInit()
	{
		if (m_table == nullptr)
		{
			if (m_arena == nullptr)
			{
				m_table = new KVP[m_slotCount];
			}
			else
			{
				m_table = (KVP*)RunAllocator(m_arena, sizeof(KVP)*m_slotCount); // new KVP[slots];
			}
			for (int i = 0; i < m_slotCount; i++) m_table[i].m_key = 0;
		}
	}

	void add(void* t, void*v)
	{
		add((size_t)t, (size_t)v);
	}

	__declspec(noinline)
		void add(size_t k, size_t v)
	{
		LazyInit();

		assert(k != 0);
	retry:
		SpinLock(m_lock);
		int key = (k >> 3) % m_slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < c_tries; i = (i == m_slotCount - 1) ? 0 : i + 1)
		{
			if (m_table[i].m_key == 0 || m_table[i].m_key == k)
			{
				m_table[i].m_key = k;
				m_table[i].m_value = v;
				SpinUnlock(m_lock);
				return;
			}
		}

		int s2 = m_slotCount * 2 - 1;

		KVP*table2 = (KVP*)RunAllocator(m_arena, sizeof(KVP)*s2); //new KVP[s2];
		for (int i = 0; i < s2; i++) table2[i].m_key = 0;
		for (int i = 0; i < s2; i++) table2[i].m_value = 0;
		for (int i = 0; i < m_slotCount; i++)
		{
			size_t n = m_table[i].m_key;
			size_t v = m_table[i].m_value;
			if (n != 0)
			{
				int key = (n >> 3) % s2;
				int cnt = 0;
				for (int i = key; cnt < c_tries; i = (i == s2 - 1) ? 0 : i + 1)
				{
					if (table2[i].m_key == 0 || table2[i].m_key == n)
					{
						table2[i].m_key = n;
						table2[i].m_value = v;
						break;
					}
				}
			}
		}

		if (m_arena == nullptr) delete m_table;
		m_table = table2;
		m_slotCount = s2;
		SpinUnlock(m_lock);
		goto retry;
	}

	void clear()
	{
		SpinLock(m_lock);
		for (int i = 0; i < m_slotCount; i++) m_table[i].m_key = 0;
		for (int i = 0; i < m_slotCount; i++) m_table[i].m_value = 0;
		SpinUnlock(m_lock);
	}


	bool containskey(void* n)
	{
		return containskey((size_t)n);
	}

	bool containskey(size_t n)
	{
		SpinLock(m_lock);
		LazyInit();
		int key = (n >> 3) % m_slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < c_tries; i = (i == m_slotCount - 1) ? 0 : i + 1)
		{
			if (m_table[i].m_key == n)
			{
				SpinUnlock(m_lock);
				return true;
			}
			if (m_table[i].m_key == 0)
			{
				SpinUnlock(m_lock);
				return false;
			}
		}

		SpinUnlock(m_lock);
		return false;
	}

	__declspec(noinline)
		size_t lookup(size_t n)
	{
		SpinLock(m_lock);
		LazyInit();
		int key = (n >> 3) % m_slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < c_tries; i = (i == m_slotCount - 1) ? 0 : i + 1)
		{
			if (m_table[i].m_key == 0 || m_table[i].m_key == n)
			{

				size_t v = m_table[i].m_value;
				SpinUnlock(m_lock);
				return v;
			}
		}
		SpinUnlock(m_lock);

		return 0;
	}

	size_t& operator[](size_t n)
	{
	retry:
		SpinLock(m_lock);
		LazyInit();
		int key = (n >> 3) % m_slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < c_tries; i = (i == m_slotCount - 1) ? 0 : i + 1)
		{
			if (m_table[i].m_key == 0 || m_table[i].m_key == n)
			{

				m_table[i].m_key = n;
				auto& ret = m_table[i].m_value;
				SpinUnlock(m_lock);
				return ret;
			}
		}
		SpinUnlock(m_lock);
		add(n, 0);
		goto retry;
	}
	static void Test();
private:
	void SpinLock(long& lock)
	{
		while (0 != InterlockedCompareExchange(&lock, 1, 0));
	}

	void SpinUnlock(long& lock)
	{
		while (1 != InterlockedCompareExchange(&lock, 0, 1));
	}

};


void ArenaHashtable::Test()
{
	ArenaHashtable h;
	for (size_t i = 1; i < 1000; i++)
	{
		size_t k = i * 0x3408;
		size_t v = i;
		h.add(k, v);
	}

	for (size_t i = 1; i < 1000; i++)
	{
		size_t k = i * 0x3408;
		if (!h.containskey(k)) throw 0;
		if (h.containskey(k - 1)) throw 0;
		if (h[k] != i) throw 0;
		h[k] = i + 1;
	}

	for (size_t i = 1; i < 1000; i++)
	{
		size_t k = i * 0x3408;
		if (h[k] != i + 1) throw 0;
	}

}
////////////////////////////////////////////////////////
// ArenaThread
//
// 
////////////////////////////////////////////////////////


class ArenaThread
{
private:
	Arena* m_arena;
	char *m_next;
	char *m_end;
	size_t m_bufferSize;

public:
	ArenaThread()
	{

	}

	ArenaThread(Arena* arena, char *next, char *end, size_t bufferSize)
	{
		if ((size_t)arena < 0x40000000000)
		{
			assert(!"bad arena pointer");
		}
		m_arena = arena;
		m_next = next;
		m_end = end;
		m_bufferSize = bufferSize;
	}

	void SetBuffer(char* next, char* end)
	{
		m_next = next;
		m_end = end;
	}

	void* Allocate(size_t size, bool report = true);
};

////////////////////////////////////////////////////////
// Arena
//
////////////////////////////////////////////////////////
class Arena
{
	struct Buffer
	{
		char* m_addr;
		size_t m_len;
	};

	friend class ArenaThread;
private:
	static const size_t ThreadSafeBufferPreallocate = 8 * 1024;
	// within the fixed address space of a single arena are individual buffers
	// which are assigned to different threads, with one reserved for thread shared access.
	// each buffer doles out memory sequentially to anyone who asks.
	Buffer *m_buffers;

	// Total number of buffers allowed per arena
	int m_bufferCnt;

	// Number of buffers uses per arena
	int m_bufferNext;

	LONG m_lock0;
	LONG m_lock1;
	LONG m_lock2;
	LONG m_lock3;
	ArenaId m_id;

	// normal buffer size used (larger is allowed for overflow allocations)
	size_t m_bufferSize;

	// address that defines the end of this arena memory addresses that we can Virtual Alloc into
	size_t m_arenaEnd;

	// The thread safe access point for the the thread that created the arena
	// other access points can be created with SpawnArenaThread
	ArenaThread m_arenaThread;
	// for allocations from any thread that does not have access to its arenathread, requires a lock to use
	ArenaThread m_sharedArenaThread;
	ArenaHashtable m_cache;

public:
	static Arena* MakeArena(ArenaId id, size_t requestAddress, size_t bufferSize, size_t maxPerArena)
	{
		LPVOID addr = ClrVirtualAlloc((LPVOID)requestAddress, bufferSize, MEM_COMMIT, PAGE_READWRITE);
		return new (addr) Arena(id, requestAddress, bufferSize, maxPerArena);
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

	ArenaThread* BaseArenaThread()
	{
		return &m_arenaThread;
	}

	ArenaThread* SpawnArenaThread()
	{
		SpinLock(m_lock2);
		ArenaThread* arenaThread = (ArenaThread*)m_arenaThread.Allocate(sizeof(ArenaThread));
		new (arenaThread) ArenaThread(this, nullptr, nullptr, m_bufferSize);
		GetNewBuffer(arenaThread);
		SpinUnlock(m_lock2);
		return arenaThread;
	}

	void SpinLock(LONG& lock)
	{
		while (0 != InterlockedCompareExchange(&lock, 1, 0));
	}

	void SpinUnlock(LONG& lock)
	{
		while (1 != InterlockedCompareExchange(&lock, 0, 1));
	}

	// c/dtor
#pragma warning(disable :  4355 )  //this used in base member initializer list

	Arena(ArenaId id, size_t addr, size_t bufferSize, size_t maxPerArena)
		: m_cache((void*)this)

	{
		assert((size_t)this == addr); // , "Arena should only be constructed through MakeArena");
		new (&m_arenaThread) ArenaThread(this, (char*)this + sizeof(Arena), (char*)this + bufferSize, bufferSize);

		m_bufferCnt = (int)(maxPerArena / bufferSize);
		m_buffers = (Buffer*)m_arenaThread.Allocate(m_bufferCnt*sizeof(Buffer*));
		m_buffers[0].m_addr = (char*)this;
		m_buffers[0].m_len = bufferSize;

		m_bufferSize = bufferSize;
		m_arenaEnd = (size_t)this + maxPerArena;
		m_bufferNext = 1;
		m_lock0 = 0;
		m_lock1 = 0;
		m_lock2 = 0;
		m_lock3 = 0;
		m_id = id;

		char* threadSafeBuffer = (char*)m_arenaThread.Allocate(ThreadSafeBufferPreallocate);
		new (&m_sharedArenaThread) ArenaThread(this, threadSafeBuffer, threadSafeBuffer + ThreadSafeBufferPreallocate, bufferSize);
	}

	char* VAlloc(size_t len)
	{
		if (m_bufferNext >= m_bufferCnt)
		{
			EEPOLICY_HANDLE_FATAL_ERROR(COR_E_OUTOFMEMORY);
		}

		void* addr = ArenaManager::CreateBuffer(m_id, len);
		SpinLock(m_lock0);
		m_buffers[m_bufferNext].m_addr = (char*)addr;
		m_buffers[m_bufferNext++].m_len = len;
		assert((size_t)addr < m_arenaEnd);
		SpinUnlock(m_lock0);
		::ArenaManager::Log("VirtualAlloc", (size_t)addr, len, nullptr, m_id);
		return (char*)addr;
	}

	char*Overflow(size_t size)
	{
		return VAlloc(size);
	}

	void GetNewBuffer(ArenaThread* arenaThread)
	{
		auto next = VAlloc(m_bufferSize);
		arenaThread->SetBuffer(next, next + m_bufferSize);
	}

	void Destroy()
	{
		for (int i = m_bufferNext - 1; i >= 0; i--)
		{
			auto addr = m_buffers[i].m_addr;
			auto len = m_buffers[i].m_len;
			ClrVirtualFree(addr, len, MEM_DECOMMIT);
			//::ArenaManager::Log("VirtualFree", (size_t)addr, len);

		}
	}

	void AddCache(Object* src, Object* copy)
	{
		SpinLock(m_lock3);
		m_cache.add((size_t)src, (size_t)copy);
		SpinUnlock(m_lock3);
	}

	Object* CheckCache(Object*src)
	{
		SpinLock(m_lock3);
		Object*ret = nullptr;
		if (m_cache.containskey(src))
		{
			ret = (Object*)m_cache[(size_t)src];
		}

		SpinUnlock(m_lock3);
		return ret;
	}

	void* ThreadSafeAllocate(size_t size)
	{
		SpinLock(m_lock1);
		auto ret = m_sharedArenaThread.Allocate(size, false);
		SpinUnlock(m_lock1);
		::ArenaManager::Log("clone Allocate", (size_t)ret, size);
		return ret;
	}
};

void* ArenaThread::Allocate(size_t size, bool report)
{
	for (;;)
	{
		char* after = m_next + size;
		if (after < m_end)
		{
			char* ret = m_next;
			m_next = after;

			memset(ret, 0, size);
			return ret;
		}
		else if (size < m_bufferSize)
		{
			m_arena->GetNewBuffer(this);

		}
		else
		{
			char* ret = m_arena->Overflow(size);
			memset(ret, 0, size);
			return ret;
		}
	}
}


//////////////////////////////////////////////
// ArenaVirtualMemory
//
//////////////////////////////////////////////


struct ArenaVirtualMemoryState
{
	BufferId m_nextSlot;
	BufferId m_startSlot;
	BufferId m_circleNextSlot;
	LONG m_lock;
};

#define ARENALOOKUP(x) (((ArenaId*)ArenaManager::arenaBaseRequest)[x])

class ArenaVirtualMemory
{
public:

private:
	static const size_t maxBuffers = ArenaManager::arenaBaseSize / ArenaManager::bufferReserveSize;
	static const ArenaId available = (ArenaId)-1;
	static const ArenaId empty = (ArenaId)-2;


	static ArenaVirtualMemoryState s;

public:

	static void Initialize()
	{
		size_t allocNeeded = maxBuffers * sizeof(ArenaId) + ArenaManager::bufferPadding * 2;
		size_t allocSize = (allocNeeded / ArenaManager::bufferReserveSize + 1)
			* ArenaManager::bufferReserveSize;

		auto virtualBase = (size_t)ClrVirtualAlloc(
			(LPVOID)ArenaManager::arenaBaseRequest,
			ArenaManager::arenaBaseRequest/*arenaBaseSize*/,
			MEM_RESERVE, PAGE_NOACCESS);

		if ((size_t)virtualBase != ArenaManager::arenaBaseRequest)
		{
			printf("failed to initialize virtual memory for arenas");
			MemoryException();
		}


		LPVOID addr = ClrVirtualAlloc((LPVOID)virtualBase, allocSize - ArenaManager::bufferPadding, MEM_COMMIT, PAGE_READWRITE);
		if (addr == 0)
		{
			int err = GetLastError();
			printf("error %d\n", err);
		}
		if ((size_t)addr != ArenaManager::arenaBaseRequest)
		{
			printf("failed to initialize virtual memory for arenas");
			MemoryException();
		}

		s.m_nextSlot = ArenaManager::bufferReserveSize / ArenaManager::bufferSize;
		s.m_startSlot = s.m_nextSlot;
		s.m_circleNextSlot = s.m_nextSlot;
	}

private:
	static BufferId Slots(size_t len)
	{
		return (BufferId)((len + ArenaManager::bufferPadding - 1) / ArenaManager::bufferReserveSize + 1);
	}

	static void SpinLock(LONG& lock)
	{
		while (0 != InterlockedCompareExchange(&lock, 1, 0));
	}

	static void SpinUnlock(LONG& lock)
	{
		while (1 != InterlockedCompareExchange(&lock, 0, 1));
	}

	static void* BufferIdToAddress(BufferId id)
	{
		return  (void*)(ArenaManager::arenaBaseRequest + id*ArenaManager::bufferReserveSize);
	}

	static BufferId BufferAddressToId(void* addr)
	{
		return (BufferId)(((size_t)addr - ArenaManager::arenaBaseRequest) / ArenaManager::bufferReserveSize);
	}

public:
	static bool IsArena(void*addr)
	{
		return ((size_t)addr >= ArenaManager::arenaBaseRequest);
	}

	static ArenaId GetArenaId(void*addr)
	{
		if (!IsArena(addr)) return -1;
		BufferId bid = BufferAddressToId(addr);
		return ARENALOOKUP(bid);
	}

	static void FreeBuffer(void* addr, size_t len = ArenaManager::bufferSize)
	{
		if (len == ArenaManager::bufferSize)
		{
			BufferId first = BufferAddressToId(addr);
			int slots = Slots(len);
			for (BufferId i = first; i < first + slots; i++)
			{
				ARENALOOKUP(i) = available;
			}
		}
		else {
			ClrVirtualFree(addr, len, MEM_DECOMMIT);
			BufferId first = BufferAddressToId(addr);
			int slots = Slots(len);
			for (BufferId i = first; i < first + slots; i++)
			{
				ARENALOOKUP(i) = empty;
			}
		}
	}

	__declspec(noinline)
		static void* GetBuffer(ArenaId arenaId, size_t len = ArenaManager::bufferSize)
	{
		assert(arenaId != empty && arenaId != available);
		SpinLock(s.m_lock);
		BufferId bufferId = 0;
		BufferId backupId = 0;
		void* ret = nullptr;

		if (len == ArenaManager::bufferSize)
		{
			int cnt = s.m_nextSlot - s.m_startSlot;
			for (BufferId i = s.m_circleNextSlot; --cnt >= 0 && ret == nullptr; i = (i == s.m_nextSlot - 1) ? s.m_startSlot : i + 1)
			{
				if (ARENALOOKUP(i) == available)
				{
					bufferId = i;
					ret = BufferIdToAddress(bufferId);
				}
				else if (ARENALOOKUP(i) == empty)
				{
					backupId = i;
				}
			}
		}


		if (ret) {
			ARENALOOKUP(bufferId) = arenaId;
			SpinUnlock(s.m_lock);
			return ret;
		}

		if (backupId == 0)
		{
			bufferId = s.m_nextSlot;
			s.m_nextSlot += Slots(len);
			if (s.m_nextSlot > maxBuffers)
			{
				MemoryException();
			}
		}
		else
		{
			bufferId = backupId;
		}

		for (int i = bufferId; i < bufferId + Slots(len); i++)
			ARENALOOKUP(i) = arenaId;
		ret = BufferIdToAddress(bufferId);
		SpinUnlock(s.m_lock);

		LPVOID addr = ClrVirtualAlloc(ret, len, MEM_COMMIT, PAGE_READWRITE);

		if (addr != ret)
		{
			printf("failed to initialize virtual memory for arenas");
			MemoryException();
		}

		return ret;
	}

private:
	static void MemoryException()
	{
		EEPOLICY_HANDLE_FATAL_ERROR(COR_E_OUTOFMEMORY);

	}
};

ArenaVirtualMemoryState ArenaVirtualMemory::s;



//////////////////////////////////////////////
// ArenaManager
//
//////////////////////////////////////////////


void ArenaManager::InitArena()
{
#ifdef DEBUG
	ArenaVector<int>::Test();
	ArenaHashtable::Test();
#ifdef VERIFYALLOC
	ArenaSet::Test();
#endif // VERIFYALLOC
#endif // DEBUG

	for (int i = 0; i < maxArenas; i++)
	{
		arenaById[i] = nullptr;
		refCount[i] = 0;
	}

	ArenaVirtualMemory::Initialize();
#ifdef VERIFYALLOC
	ClrVirtualAlloc((LPVOID)0x60000000000, 1024 * 1024 * 1024, MEM_COMMIT, PAGE_READWRITE);
	*(size_t*)0x60000000000 = 0x60000000008;
#endif // VERIFYALLOC
}

ArenaStack& ArenaManager::GetArenaStack()
{
	return GetThread()->m_arenaStack;
}

ArenaStack& ArenaManager::GetArenaStack(Thread* thread)
{
	return thread->m_arenaStack;
}

#ifdef VERIFYALLOC

void ArenaManager::VerifyAllArenaObjects()
{
	verifiedObjects.clear();
	auto p = (size_t**)0x60000000000;
	for (auto i = (size_t*)0x60000000008; i < *p; i++)
	{
		VerifyObject(*(Object**)i);
	}

	}
#endif // VERIFYALLOC

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

ArenaId ArenaManager::GetArenaId()
{
	Arena* arena = (Arena*)GetArenaStack().Current();
	if (arena == nullptr) return -1;
	return ArenaVirtualMemory::GetArenaId(arena);
}

void _cdecl ArenaManager::SetAllocator(unsigned int type)
{
	ArenaStack& arenaStack = GetArenaStack();
	ArenaThread* current_arena;
	switch (type)
	{
	case 1:
		for (int i = 0; i < arenaStack.Size(); i++)
		{
			DereferenceId(ArenaVirtualMemory::GetArenaId(arenaStack[i]));
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

void* ArenaManager::CreateBuffer(ArenaId arenaId, size_t len)
{
	return ArenaVirtualMemory::GetBuffer(arenaId, len);
}

Arena* ArenaManager::MakeArena()
{
	ArenaId id = getId();
	void* arenaBase = ArenaVirtualMemory::GetBuffer(id);

	Arena* arena = Arena::MakeArena(id, (size_t)arenaBase, ArenaManager::bufferSize, ArenaManager::arenaBaseSize / 4);
	//Log("Arena is registered ", (size_t)arena);
	arenaById[id] = arena;
	return arena;
}


void ArenaManager::DeleteAllocator(void* vallocator)
{
	if (vallocator == nullptr) return;

	ArenaId id = ArenaVirtualMemory::GetArenaId(vallocator);
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

static ArenaId ArenaNumber(void*addr)
{
	return ArenaVirtualMemory::GetArenaId(addr);
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
#endif // LOGGING
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
void ArenaManager::ArenaMarshal (void*idst, void*isrc)
{
	STATIC_CONTRACT_MODE_COOPERATIVE;
	STATIC_CONTRACT_NOTHROW;
	STATIC_CONTRACT_GC_TRIGGERS;
	STATIC_CONTRACT_SO_TOLERANT;

	if (isrc == nullptr) return;
	Thread* thread = GetThread();

	ArenaVector<MarshalRequest> queue;
	queue.push_back(MarshalRequest((Object*)isrc, (Object**)idst));

	while (!queue.empty())
	{
		MarshalRequest request = queue.pop_front();
		queue.delete_repeat(request);

		Object *src = request.src;
		Object **dst = request.dst;

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
#endif // LOGGING

		// if the destination address is in a GC HEAP, then dstAllocator == nullptr
		Arena* dstAllocator = (Arena*)AllocatorFromAddress(dst);
		Arena* srcAllocator = (Arena*)AllocatorFromAddress(src);

		// note: caching is NOT supported on arena-to-arena clones, only from GC to arena or arena to GC
		Arena* arenaAllocator = (dstAllocator == nullptr) ? srcAllocator : ((srcAllocator == nullptr) ? dstAllocator : nullptr);

		Object* clone = nullptr;
		bool cloneIsFromCache = false;

		// check cache
		if (arenaAllocator != nullptr)
		{
			clone = arenaAllocator->CheckCache(src);
			if (clone)
			{
				cloneIsFromCache = true;
#ifdef LOGGING
				Log("cached clone", (size_t)src, (size_t)clone, "", (size_t)idst);
#endif // LOGGING
			}
		}

		if (!clone)
		{

			// Allocate memory for clone from either GC or arena
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
#endif // AMD64

				memcpyNoGCRefs(clone, src, ioffset);
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
#endif // LOGGING
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
#endif // DEBUG


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
#endif // LOGGING
#ifdef VERIFYALLOC
			if (!ISARENA(clone))
			{
				RegisterAddress(clone);
			}
			CheckAll();
#endif // VERIFYALLOC
			}

		if (!cloneIsFromCache)
		{
			arenaAllocator->AddCache(src, clone);
			arenaAllocator->AddCache(clone, src);
		}

		if (valueTypeSize == 0)
		{
			*dst = clone;
			if (dstAllocator == nullptr)
			{

#ifdef _DEBUG
				Thread::ObjectRefAssign((OBJECTREF *)dst);
#endif // _DEBUG
				ErectWriteBarrier(dst, clone);
			}
		}
	}
}

#ifdef VERIFYOBJECT
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
#endif // _DEBUG


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

#endif // VERIFYOBJECT

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
		return new char[len];
	}
	else
	{
		return ((Arena*)allocator)->ThreadSafeAllocate(len);
	}
}