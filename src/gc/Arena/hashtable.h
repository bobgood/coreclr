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
public:
	set(int slots=100)
	{
		slotCount = slots;
		table = new size_t[slots];
		for (int i = 0; i < slots; i++) table[i].key = 0;
	}

	void add(void* t, void*v)
	{
		add((size_t)t, (size_t)v);
	}

	__declspec(noinline)
	void add(size_t k, size_t v)
	{
		retry:
		SpinLock(lock);
		int key = (n>>3)%slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < 10; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i].key == 0 || table[i].key == n)
			{
				table[i].key = n;
				table[i].value = v;
				SpinUnlock(lock);
				return;
			}
		}

		int s2 = slotCount * 2;
		size_t*table2 = new size_t[s2];
		for (int i = 0; i < s2; i++) table2[i] = 0;
		for (int i = 0; i < slotCount; i++)
		{
			size_t n = table[i].key;
			size_t v = table[i].value;
			if (n != 0)
			{
				int key = (n>>3)%s2;
				int cnt = 0;
				for (int i = key; cnt < 10; i = (i == s2 - 1) ? 0 : i + 1)
				{
					if (table2[i] == 0 || table2[i] == n)
					{
						table2[i].key = n;
						table2[i].value = v;
						break;
					}
				}

			}
		}

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

	
	bool contains(void* n)
	{
		return contains((size_t)n);
	}

	__declspec(noinline)
	size_t lookup(size_t n)
	{
		SpinLock(lock);
		int key = (n>>3)%slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < 10; i = (i == slotCount - 1) ? 0 : i + 1)
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
		for (int i = key; cnt++ < 10; i = (i == slotCount - 1) ? 0 : i + 1)
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
	auto hashtable h = new hashtable();

}