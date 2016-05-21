#pragma once

#include "windows.h"
class set
{
	size_t* table;
	int slotCount;
	long lock;
	static const int tries = 5;
public:
	set(int slots=97)
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
		int key = (n>>3)%slotCount;
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
				int key = (m>>3)%s2;
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
		int key = (n>>3)%slotCount;
		int cnt = 0;
		for (int i = key; cnt++ < tries; i = (i == slotCount - 1) ? 0 : i + 1)
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

public:
	static void Test()
	{
		set s;
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