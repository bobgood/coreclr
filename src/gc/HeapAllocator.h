#pragma once
// Hack


#pragma warning(push)
// warning C4355: 'this' : used in base member initializer list
#pragma warning(disable: 4355)

namespace sfl
{

	class HeapAllocator

	{
	public:
		// Standard Library allocator adapter

		struct Config
		{};

		HeapAllocator(const Config& = Config())

		{}

		void* Allocate(size_t size)
		{
			// Hack
			return nullptr;
			//        return malloc(size);
		}

		void Deallocate(void* ptr)
		{
			//      free(ptr);
		}

		void Reset()
		{}

		bool operator==(const HeapAllocator&) const
		{
			return true;
		}
	};


} // namespace sfl

#pragma warning(pop)
