#pragma once

#include "AtomicMutex.h"

#include <mutex>
#include <thread>
#include <cstring>
#include <utility>

#define DEFAULT_MEMPOOL_SIZE 16

namespace JbSystem{

    template <typename Tag/* Used to create more memory pools local to a specific combination of parameters, this will reduce pressure */, typename T> struct MemoryPool
    {
      private:
        uint32_t size;
        uint32_t capacity;

        // Contains large memory blocks
        // This is where the actual memory lives.
        std::vector<T*> markers;

        // Contains the available memory addresses
        // When we need a new memory address, we will
        // select the address available at the top of this stack.
        T** memstack;

        JbSystem::Mutex mutex;

      public:
        MemoryPool(uint32_t size = DEFAULT_MEMPOOL_SIZE) :
            size(0),
            capacity(size)
        {
            T* new_block = (T*)calloc(capacity, sizeof(T));
            markers.push_back(new_block);

            memstack = (T**)calloc(capacity, sizeof(T*));
            // Fill the stack with the available memory addresses.
            for (uint32_t i = 0; i < capacity; i++)
            {
                memstack[i] = new_block + i;
            }
        }

        ~MemoryPool()
        {
            for (uint32_t i = 0; i < markers.size(); i++)
            {
                free(markers[i]);
            }

            free(memstack);
        }

        T* Alloc()
        {
            std::lock_guard lock(mutex);
            if (size == capacity)
            {
                // Free the old stack of addresses
                free(memstack);

                // Allocated memory has filled, reallocate memory
                memstack = (T**)calloc(2 * capacity, sizeof(T*));

                T* newBlock = (T*)calloc(capacity, sizeof(T));

                // Keep track of the large memory blocks for destructor
                markers.push_back(newBlock);

                // Record the newly available addresses in the stack
                // Note that we don't care about the older addresses
                // since they are already allocated and given out.
                for (uint32_t i = 0; i < capacity; i++)
                {
                    memstack[capacity + i] = newBlock + i;
                }

                capacity *= 2;
            }

            T* next = memstack[size++];
            memset(next, 0, sizeof(T));
            return next;
        }

        void Free(T* mem)
        {
            std::lock_guard lock(mutex);

            mem->~T();

            // mem location is now available
            // Add that at the top of the stack
            memstack[--size] = mem;
        }

        int Size() {
            std::lock_guard lock(mutex);

            return size;
            }
        int Capacity() {
            std::lock_guard lock(mutex);

            return capacity;
            }

        static MemoryPool& Get(){
            static MemoryPool pool;
            return pool;
        }
    };
}