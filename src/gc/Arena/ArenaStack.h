// Hack
#pragma once

class ArenaStack
{
	// Depth of fixed stack depth for arenas in each thread
	static const int arenaStackDepth = 40;
	void* current = nullptr;
	void* stack[arenaStackDepth];
	int size = 0;
public:
	bool freezelog=false;

public:
	void* Current()
	{
		return current;
	}

	void Reset()
	{
		size = 0;
		current = nullptr;
	}

	int Size()
	{
		return size;
	}

	void* operator[](int offset)
	{
		if (offset < 0 || offset >= size)
		{
			assert(offset >= 0 && offset < size);
		}

		return stack[offset];
	}

	void Push(void* v)
	{
		current = v;
		stack[size++] = v;
		if (size >= arenaStackDepth)
		{
			assert(size < arenaStackDepth);
		}
	}

	void* Pop()
	{
		if (size < 1)
		{
			assert(size > 0);
		}
		current = stack[--size];
		return current;
	}
};

