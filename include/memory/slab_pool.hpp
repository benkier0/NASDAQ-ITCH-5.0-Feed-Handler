#pragma once
#include <array>
#include <cstddef>
#include <cassert>
#include <new>

namespace memory {

template<typename T, std::size_t Capacity>
class SlabPool {
    union Slot {
        alignas(T) std::byte storage[sizeof(T)];
        Slot* next;
    };

    std::array<Slot, Capacity> slots_;
    Slot* free_head_{nullptr};
    std::size_t allocated_{0};

public:
    SlabPool() noexcept {
        for (std::size_t i = 0; i < Capacity - 1; ++i)
            slots_[i].next = &slots_[i + 1];
        slots_[Capacity - 1].next = nullptr;
        free_head_ = &slots_[0];
    }

    template<typename... Args>
    [[nodiscard]] T* acquire(Args&&... args) noexcept {
        if (__builtin_expect(free_head_ == nullptr, 0)) return nullptr;
        Slot* slot = free_head_;
        free_head_ = slot->next;
        ++allocated_;
        return ::new (slot->storage) T(static_cast<Args&&>(args)...);
    }

    void release(T* ptr) noexcept {
        assert(ptr != nullptr);
        ptr->~T();
        auto* slot = reinterpret_cast<Slot*>(ptr);
        slot->next = free_head_;
        free_head_ = slot;
        --allocated_;
    }

    [[nodiscard]] std::size_t available() const noexcept { return Capacity - allocated_; }
    [[nodiscard]] std::size_t allocated() const noexcept { return allocated_; }
    [[nodiscard]] std::size_t capacity()  const noexcept { return Capacity; }
};

} // namespace memory
