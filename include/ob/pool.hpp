#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ob {

// Single-threaded free-list pool for fixed-size allocations.
// Grows in blocks; O(1) acquire/release; no malloc on the hot path in steady
// state.  Used as the backing store for PoolAllocator (below), which plugs
// into std::list to eliminate per-node malloc/free.
class FreeListPool {
public:
    explicit FreeListPool(std::size_t block_count = 8192)
        : block_count_(block_count) {}

    ~FreeListPool() {
        for (auto* b : blocks_) ::operator delete(b);
    }

    FreeListPool(const FreeListPool&) = delete;
    FreeListPool& operator=(const FreeListPool&) = delete;

    void* allocate(std::size_t bytes, std::size_t alignment) {
        if (slot_size_ == 0) {
            slot_size_ = bytes < sizeof(FreeNode) ? sizeof(FreeNode) : bytes;
            slot_size_ = (slot_size_ + alignment - 1) & ~(alignment - 1);
        }
        if (bytes <= slot_size_) {
            if (!head_) grow();
            auto* node = head_;
            head_ = node->next;
            return node;
        }
        return ::operator new(bytes);
    }

    void deallocate(void* p, std::size_t bytes) {
        if (bytes <= slot_size_) {
            auto* node = static_cast<FreeNode*>(p);
            node->next = head_;
            head_ = node;
        } else {
            ::operator delete(p);
        }
    }

private:
    struct FreeNode { FreeNode* next; };

    void grow() {
        auto* raw = static_cast<char*>(::operator new(block_count_ * slot_size_));
        blocks_.push_back(raw);
        for (std::size_t i = 0; i < block_count_; ++i) {
            auto* node = reinterpret_cast<FreeNode*>(raw + i * slot_size_);
            node->next = head_;
            head_ = node;
        }
    }

    std::size_t block_count_;
    std::size_t slot_size_ = 0;
    FreeNode* head_ = nullptr;
    std::vector<char*> blocks_;
};

// STL-compatible allocator backed by a FreeListPool.
// When pool_ is nullptr, falls back to global operator new/delete.
// The pool is shared across rebind types (std::list rebinds Order -> _List_node<Order>).
template <typename T>
class PoolAllocator {
public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    PoolAllocator() noexcept : pool_(nullptr) {}
    explicit PoolAllocator(FreeListPool* pool) noexcept : pool_(pool) {}

    template <typename U>
    PoolAllocator(const PoolAllocator<U>& other) noexcept : pool_(other.pool_) {}

    T* allocate(std::size_t n) {
        if (pool_ && n == 1)
            return static_cast<T*>(pool_->allocate(sizeof(T), alignof(T)));
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (pool_ && n == 1) {
            pool_->deallocate(p, sizeof(T));
        } else {
            ::operator delete(p);
        }
    }

    FreeListPool* pool_;
};

template <typename T, typename U>
bool operator==(const PoolAllocator<T>& a, const PoolAllocator<U>& b) noexcept {
    return a.pool_ == b.pool_;
}

template <typename T, typename U>
bool operator!=(const PoolAllocator<T>& a, const PoolAllocator<U>& b) noexcept {
    return a.pool_ != b.pool_;
}

} // namespace ob
