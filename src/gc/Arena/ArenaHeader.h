#pragma once

#include "ArenaManager.h"
#include "ArenaAllocator.h"
#include "ObjectHashTable.h"
#include "Object.h"


class ArenaHeader
{
public:
	Arena m_arena;
	ObjectHashTable* clones[ArenaManager::maxArenas];

	ArenaHeader(Arena&& arena)
	{
		new (&m_arena) Arena(arena);
		for (int i = 0; i < ArenaManager::maxArenas; i++)
		{
			clones[i] = nullptr;
		}
	}
};