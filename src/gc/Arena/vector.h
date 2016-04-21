#pragma once
#include <assert.h>
// Hack

void* RunAllocator(void* allocator, size_t len);


namespace sfl
{
	template<class T>
	class vector
	{
	public:
		static const int fixedsize = 10;
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
		void* arenaAllocator;
	public:
		vector()

		{
			arenaAllocator = nullptr;
			reserved = fixedsize;
			array = nullptr;
			isize = 0;
		}

		~vector()
		{
			//arenas ignore delete
		}

		void setAllocator(void* a)
		{
			arenaAllocator = a;
		}

		void reserve(size_t n)
		{
			assert(arenaAllocator != nullptr);

			if (reserved > n) return;  // never shrink when arenas are involved
			if (array == nullptr) array = fixed;
			T* arr2 = (T*)RunAllocator(arenaAllocator, sizeof(T)*n);
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
			if (array == nullptr)
			{
				fixed[isize++] = v;
			}
			else {
				array[isize++] = v;
			}
		}

		T& operator[](size_t n)
		{
			assert(n < isize);
			if (array == nullptr)
			{
				return fixed[n];
			}
			else
			{
				return array[n];
			}
		}
	};
}
