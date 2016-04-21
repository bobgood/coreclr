#pragma once

#include "object.h"
#include "simplehashtable.h"
#include "ArenaAllocator.h"
#include "lock.h"

class ObjectHashTable : SimpleHashTable<Object*, SimpleHashPolicy::SingleThreaded>
{

public:
	static ObjectHashTable* Create(Arena& arena)
	{
		ObjectHashTable* t = (ObjectHashTable*)arena.Allocate(sizeof(ObjectHashTable));
		new (t) ObjectHashTable(arena);
		return t;
	}

	// Constructs a SimpleHashTable2Base with initial hash table size based
	// on the capaity param. For best performance the capacity should be
	// set to about twice the number of items to be stored in the table.
	// If allowResize is set to true, the table will automatically grow
	// as more items are added. Note that the ThreadSafe policy does not
	// support resizing and will cause an exception in the constructor when
	// the allowResize parameter is true.
	ObjectHashTable(Arena& arena)
		: SimpleHashTable(100, true, arena)
	{

	}
	~ObjectHashTable()
	{}

	static unsigned __int64 Hash(const Object* key)
	{
		return _rotr64((size_t)key,3);
	}

	static Object* UnHash(const unsigned __int64 key)
	{
		return (Object*)_rotl64((size_t)key,3);
	}

	// Returns a reference to the POD type value associated with key.
	// If key is not already in the hash table, it will be added and its
	// pointer-typed value will be initialized to nullptr. If there is not
	// enough space for the new key and resize is allowed, the key-value
	// buffers will be reallocated and rehashed.
	Object*& operator[](const Object* key)
	{
		sfl::lock_guard<sfl::critical_section> guard(m_lock);
		return SimpleHashTable::operator[](Hash(key));
	}

	// Searches the hash table for a specified key. If the key is found,
	// the found parameter will be set to true, and a reference to the 
	// POD type value associated with the key will be returned.
	// Otherwise, the found parameter will be set to false, and an
	// unspecified reference will be returned.
	Object*& Find(const Object* key , bool &found)
	{
		sfl::lock_guard<sfl::critical_section> guard(m_lock);
		return SimpleHashTable::Find(Hash(key), found);
	}

	// Delete the POD type value associated with a specific key from the
	// hash table. If the key is not found, no operation is performed.
		void Delete(Object* key)
	{
		sfl::lock_guard<sfl::critical_section> guard(m_lock);
		return SimpleHashTable::Delete(Hash(key));
	}

	sfl::critical_section    m_lock;
};
