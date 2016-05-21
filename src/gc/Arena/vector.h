#pragma once
#include <assert.h>
// Hack

void* RunAllocator(void* allocator, size_t len);


template<class T>
class vector
{
	static const int fixedsize = 10;
public:
	// array is set to nullptr when fixed array is used, in order to support move semantics before much data
	// is added, when move semantics would not update the pointer.
	T* array;
	// Prebuild space into the class so that the vector can be used before there is an allocator ready
	T fixed[fixedsize];
	size_t isize;
	size_t reserved;

	// arena is stored as a void* becuase vector both needs to use arena, and is a component of arena.
	// both classes are templated, so vector cannot use the arena class within the .h file, and the method
	// RunAllocator is used to access the allocator without knowing its type.
public:
	vector()

	{
		reserved = fixedsize;
		array = fixed;
		isize = 0;
	}

	~vector()
	{
		if (array != fixed) delete array;
	}

	void reserve(size_t n)
	{
		if (reserved > n) return;  // never shrink when arenas are involved
		T* arr2 = new T[n];
		memset(arr2, 0, sizeof(T)*n);
		for (int i = 0; i < reserved; i++)
		{
			arr2[i] = array[i];
		}

		reserved = n;

		array = arr2;
	}

	void resize(size_t n)
	{
		if (n > reserved) reserve(n * 3 / 2);
		isize = n;
	}

	void Reset()
	{
		resize(0);
	}

	bool empty()
	{
		return isize == 0;
	}

	size_t size()
	{
		return isize;
	}

	size_t capacity()
	{
		return reserved;
	}

	void push_back(T v)
	{
		if (isize >= reserved)
		{
			reserve(reserved < 10 ? 10 : reserved * 3 / 2);
		}
		array[isize++] = v;
	}

	T pop_front()
	{
		assert(!empty());
		T ret = array[0];
		for (int i = 1; i < isize; i++)
		{
			array[i - 1] = array[i];
		}

		isize--;
		return ret;
	}

	
	void delete_repeat(T& ref)
	{
		for (int i = 0; i < isize; i++)
		{
			T& src = array[i];
			if (src==ref)
			{
				for (int j = i + 1; j < isize; j++)
				{
					array[j - 1] = array[j];
				}
				isize--;
				i--;
			}
		}
	}

	T& operator[](size_t n)
	{
		assert(n < isize);
		return array[n];
	}

	static Test()
	{
		vector<int> n;
		for (int i = 0; i < 100; i++)
		{
			n.push_back(i);
		}

		for (int i = 0; i < 100; i++)
		{
			n.push_back(i);
		}

		for (int i = 0; i < 100; i++)
		{
			n.push_back(i);
		}

		if (n.size() != 300) throw 0;
		for (int i = 0; i < 300; i++)
		{
			if (n[i] != i % 100) throw 0;
		}

		for (int i = 0; i < 100; i++)
		{
			if (n.empty()) throw 0;
			int k = n.pop_front();
			if (k != i) throw 0;
			n.delete_repeat(i);
		}

		if (!n.empty())
		{
			int k = n.pop_front();
			throw(0);
		}

	}
};

