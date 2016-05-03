// Hack
#pragma once
#include "../common.h"
#include "arenastack.h"
#include <assert.h>

void* ArenaStack::Current()
{
	return current;
}

void ArenaStack::Reset()
{
	size = 0;
	current = nullptr;
}

int ArenaStack::Size()
{
	return size;
}

void* ArenaStack::operator[](int offset)
{
	if (offset >= arenaStackDepth) return nullptr;
	//assert(offset >= 0 && offset < size);
	return stack[offset];
}

void ArenaStack::Push(void* v)
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

void* ArenaStack::Pop()
{
	DWORD written;
	assert(size > 0);
	auto ret = operator[](--size);
	if (size == 0) current = nullptr;
	current = operator[](size - 1);
	return ret;
}


