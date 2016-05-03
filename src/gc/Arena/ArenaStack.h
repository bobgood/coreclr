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
	void* Current();
	void Reset();

	int Size();

	void* operator[](int offset);

	void Push(void* v);
	void* Pop();
};

