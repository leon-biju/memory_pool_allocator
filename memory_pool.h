#ifndef MEMORY_POOL_ALLOCATOR_MEMORY_POOL_H
#define MEMORY_POOL_ALLOCATOR_MEMORY_POOL_H


template <typename T, std::size_t Capacity>
class MemoryPool {
    // don't want default construction for all the elements at time of construction
    // so we do raw bytes
    alignas(T) std::byte storage_[sizeof(T) * Capacity]{};

    std::size_t free_list_[Capacity]{}; // free_list[0 to free_count-1] contains all free blocks
    std::size_t free_count_ = Capacity;

    bool allocated_[Capacity]{}; //Used for checking double frees

public:
    MemoryPool() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            free_list_[i] = i;
        }
    }
    ~MemoryPool() = default;

    // no copy or moves allowed
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    T* allocate() {
        if (free_count_ == 0) {
            return nullptr;
        }
        auto index = free_list_[--free_count_];

        allocated_[index] = true;

        return reinterpret_cast<T*>(&storage_[ index * sizeof(T) ]);
    }
    void deallocate(T* ptr) {
        // Freeing nullptr is well-defined and just does nothing as of the C++ standard
        if (ptr == nullptr) {return;}

        auto* raw_ptr = reinterpret_cast<std::byte*> (ptr);
        if (raw_ptr < storage_ || raw_ptr >= storage_ + Capacity * sizeof(T)) {
            throw std::runtime_error(
                std::format("Pointer {} did not originate from this pool!", reinterpret_cast<std::size_t>(raw_ptr))
                );
        }

        std::size_t index = (raw_ptr - storage_) / sizeof(T);


        if (allocated_[index] == false) {
            throw std::runtime_error(
                std::format("Double free for pointer: {}", reinterpret_cast<std::size_t>(raw_ptr))
                );
        }

        allocated_[index] = false;

        free_list_[free_count_++] = index;
    }

    [[nodiscard]] std::size_t size() const { return Capacity - free_count_; }
    [[nodiscard]] bool empty() const { return free_count_ == Capacity; }
    [[nodiscard]] bool full() const { return free_count_ == 0; }
};


#endif //MEMORY_POOL_ALLOCATOR_MEMORY_POOL_H