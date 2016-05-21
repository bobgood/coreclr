// Hack
#pragma once

class ArenaStack
{
	// Depth of fixed stack depth for arenas in each thread
	// The stack can go deeper than this amount, but only
	// nullptr can be put into the deep stack, and thus
	// no storage is needed for this.
	static const int arenaStackDepth = 10;
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
		if (offset >= arenaStackDepth) return nullptr;
		//assert(offset >= 0 && offset < size);
		return stack[offset];
	}

	void Push(void* v)
	{

		current = v;
		if (size >= arenaStackDepth)
		{
			assert(v == nullptr);
			size++;
		}
		else {
			stack[size++] = v;
		}
	}

	void* Pop()
	{
		DWORD written;
		assert(size > 0);
		auto ret = operator[](--size);
		if (size == 0) current = nullptr;
		current = operator[](size - 1);
		return ret;
	}
};

