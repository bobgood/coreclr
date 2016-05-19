#pragma once

#include "windows.h"
class set
{
	size_t* table;
	int slotCount;
	long lock;
public:
	set(int slots=100)
	{
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
		retry:
		SpinLock(lock);
		int key = (n>>3)%slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < 10; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i] == 0 || table[i] == n)
			{
				table[i] = n;
				SpinUnlock(lock);
				return;
			}
		}

		int s2 = slotCount * 2;
		size_t*table2 = new size_t[s2];
		for (int i = 0; i < s2; i++) table2[i] = 0;
		for (int i = 0; i < slotCount; i++)
		{
			size_t m = table[i];
			if (m != 0)
			{
				int key = (n>>3)%s2;
				int cnt = 0;
				for (int i = key; cnt < 10; i = (i == s2 - 1) ? 0 : i + 1)
				{
					if (table2[i] == 0 || table2[i] == n)
					{
						table2[i] = n;
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
		int key = (n>>3)%slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < 10; i = (i == slotCount - 1) ? 0 : i + 1)
		{
			if (table[i] == 0 || table[i] == n)
			{
				
				bool r = table[i]==n;
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
	
};