#pragma once

#include <atomic>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace llq {

// A bounded single-producer/single-consumer ring buffer. Exactly one producer
// may call try_push and exactly one consumer may call try_pop concurrently.
template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity)
        : slots_(checked_storage_size(capacity)), capacity_(capacity) {}

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    [[nodiscard]] bool try_push(const T& value) {
        return emplace(value);
    }

    [[nodiscard]] bool try_push(T&& value) {
        return emplace(std::move(value));
    }

    [[nodiscard]] bool try_pop(T& output) {
        const std::size_t head = head_.value.load(std::memory_order_relaxed);
        if (head == tail_.value.load(std::memory_order_acquire)) {
            return false;
        }

        output = std::move(*slots_[head]);
        slots_[head].reset();
        head_.value.store(increment(head), std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.value.load(std::memory_order_acquire) ==
               tail_.value.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t cache_line_size = 64;

    struct alignas(cache_line_size) PaddedIndex {
        std::atomic<std::size_t> value{0};
    };

    [[nodiscard]] static std::size_t checked_storage_size(std::size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("SpscQueue capacity must be greater than zero");
        }
        if (capacity == std::numeric_limits<std::size_t>::max()) {
            throw std::length_error("SpscQueue capacity is too large");
        }
        return capacity + 1;
    }

    template <typename U>
    bool emplace(U&& value) {
        const std::size_t tail = tail_.value.load(std::memory_order_relaxed);
        const std::size_t next = increment(tail);
        if (next == head_.value.load(std::memory_order_acquire)) {
            return false;
        }

        slots_[tail].emplace(std::forward<U>(value));
        tail_.value.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t increment(std::size_t index) const noexcept {
        ++index;
        return index == slots_.size() ? 0 : index;
    }

    std::vector<std::optional<T>> slots_;
    const std::size_t capacity_;
    PaddedIndex head_{};
    PaddedIndex tail_{};
};

}  // namespace llq
