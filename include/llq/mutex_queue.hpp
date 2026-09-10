#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace llq {

// A bounded, non-blocking queue protected by one mutex. This deliberately
// simple implementation serves as the correctness and performance baseline
// for the lock-free queues in this project.
template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity)
        : slots_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("MutexQueue capacity must be greater than zero");
        }
    }

    MutexQueue(const MutexQueue&) = delete;
    MutexQueue& operator=(const MutexQueue&) = delete;
    MutexQueue(MutexQueue&&) = delete;
    MutexQueue& operator=(MutexQueue&&) = delete;

    [[nodiscard]] bool try_push(const T& value) {
        std::scoped_lock lock(mutex_);
        return emplace_locked(value);
    }

    [[nodiscard]] bool try_push(T&& value) {
        std::scoped_lock lock(mutex_);
        return emplace_locked(std::move(value));
    }

    [[nodiscard]] bool try_pop(T& output) {
        std::scoped_lock lock(mutex_);
        if (size_ == 0) {
            return false;
        }

        output = std::move(*slots_[head_]);
        slots_[head_].reset();
        head_ = increment(head_);
        --size_;
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return slots_.size();
    }

    [[nodiscard]] std::size_t size() const {
        std::scoped_lock lock(mutex_);
        return size_;
    }

    [[nodiscard]] bool empty() const {
        return size() == 0;
    }

    [[nodiscard]] bool full() const {
        return size() == capacity();
    }

private:
    template <typename U>
    bool emplace_locked(U&& value) {
        if (size_ == slots_.size()) {
            return false;
        }

        slots_[tail_].emplace(std::forward<U>(value));
        tail_ = increment(tail_);
        ++size_;
        return true;
    }

    [[nodiscard]] std::size_t increment(std::size_t index) const noexcept {
        ++index;
        return index == slots_.size() ? 0 : index;
    }

    mutable std::mutex mutex_;
    std::vector<std::optional<T>> slots_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};
};

}  // namespace llq
