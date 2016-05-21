#pragma once

#include "windows.h"

struct KVP
{
	size_t key;
	size_t value;
};

class hashtable
{
	KVP* table;
	int slotCount;
	long lock;
	static const int tries = 5;
public:
	hashtable(int slots=100)
	{
		lock = 0;
		slotCount = slots;
		table = new KVP[slots];
		for (int i = 0; i < slots; i++) table[i].key = 0;
	}

	void add(void* t, void*v)
	{
		add((size_t)t, (size_t)v);
	}

	__declspec(noinline)
	void add(size_t k, size_t v)
	{
		assert(k != 0);
		retry:
		SpinLock(lock);
		int key = (k>>3)%slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < tries; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i].key == 0 || table[i].key == k)
			{
				table[i].key = k;
				table[i].value = v;
				SpinUnlock(lock);
				return;
			}
		}

		int s2 = slotCount * 2 - 1;
		KVP*table2 = new KVP[s2];
		for (int i = 0; i < s2; i++) table2[i].key = 0;
		for (int i = 0; i < s2; i++) table2[i].value = 0;
		for (int i = 0; i < slotCount; i++)
		{
			size_t n = table[i].key;
			size_t v = table[i].value;
			if (n != 0)
			{
				int key = (n>>3)%s2;
				int cnt = 0;
				for (int i = key; cnt < tries; i = (i == s2 - 1) ? 0 : i + 1)
				{
					if (table2[i].key == 0 || table2[i].key == n)
					{
						table2[i].key = n;
						table2[i].value = v;
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
		for (int i = 0; i < slotCount; i++) table[i].key = 0;
		for (int i = 0; i < slotCount; i++) table[i].value = 0;
		SpinUnlock(lock);
	}

	
	bool containskey(void* n)
	{
		return containskey((size_t)n);
	}

	bool containskey(size_t n)
	{
		SpinLock(lock);
		int key = (n >> 3) % slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < tries; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i].key == n)
			{
				SpinUnlock(lock);
				return true;
			}
			if (table[i].key == 0)
			{
				SpinUnlock(lock);
				return false;
			}
		}

		SpinUnlock(lock);
		return false;
	}

	__declspec(noinline)
	size_t lookup(size_t n)
	{
		SpinLock(lock);
		int key = (n>>3)%slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < tries; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i].key == 0 || table[i].key == n)
			{
				
				size_t v = table[i].value;
				SpinUnlock(lock);
				return v;
			}
		}
		SpinUnlock(lock);
		
		return 0;
	}

	size_t& operator[](size_t n)
	{
		retry:
		SpinLock(lock);
		int key = (n >> 3) % slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < tries; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i].key == 0 || table[i].key == n)
			{

				table[i].key = n;
				auto& ret = table[i].value;
				SpinUnlock(lock);
				return ret;
			}
		}
		SpinUnlock(lock);
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


void hashtable::Test()
{
	hashtable h;
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
		if (h[k] != i+1) throw 0;
	}

}