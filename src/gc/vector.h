#pragma once
// Hack
namespace sfl
{
	template<class T>
	class vector
	{
		static const int fixedsize = 100;
		T array[fixedsize];
		size_t isize;
		size_t reserved;
	public:
		vector()
		{
			reserved = 0;
			isize = 0;
		}

		~vector()
		{
			//delete array;
		}
		
		void reserve(size_t n)
		{
			if (n > fixedsize) throw 0;
			//if (reserved > n) return;
			//T* arr2 = new T[n];
			//for (int i = 0; i < reserved; i++)
			//{
			//	arr2[i] = array[i];
			//}
			reserved = n;
			//delete array;
			//array = arr2;

		}

		void resize(size_t n)
		{
			isize = 0;
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

		void push_back(T&& v)
		{
			if (isize >= reserved)
			{
				reserve(reserved < 10 ? 10 : reserved * 3 / 2);
			}
			array[isize++] = v;
		}

		T& operator[](size_t n)
		{
			return array[n];
		}
	};
}
