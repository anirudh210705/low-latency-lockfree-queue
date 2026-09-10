#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace llq {

// Dmitry Vyukov's bounded MPMC queue. Each slot's sequence number identifies
// its generation; relaxed CAS reserves tickets, while release/acquire on the
// sequence publishes and consumes payloads.
template <typename T>
class MpmcQueue {
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "MpmcQueue requires nothrow move construction");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "MpmcQueue requires nothrow move assignment");

public:
    explicit MpmcQueue(std::size_t capacity)
        : capacity_(checked_capacity(capacity)), mask_(capacity_ - 1), cells_(new Cell[capacity_]) {
        for (std::size_t index = 0; index < capacity_; ++index) {
            cells_[index].sequence.store(index, std::memory_order_relaxed);
        }
    }

    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;
    MpmcQueue(MpmcQueue&&) = delete;
    MpmcQueue& operator=(MpmcQueue&&) = delete;

    [[nodiscard]] bool try_push(const T& value)
        requires std::is_nothrow_copy_constructible_v<T>
    {
        return emplace(value);
    }

    [[nodiscard]] bool try_push(T&& value) {
        return emplace(std::move(value));
    }

    [[nodiscard]] bool try_pop(T& output) noexcept {
        std::size_t position = dequeue_position_.value.load(std::memory_order_relaxed);
        Cell* cell = nullptr;

        for (;;) {
            cell = &cells_[position & mask_];
            const std::size_t sequence = cell->sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t>(sequence) -
                                    static_cast<std::intptr_t>(position + 1);

            if (difference == 0) {
                if (dequeue_position_.value.compare_exchange_weak(
                        position, position + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = dequeue_position_.value.load(std::memory_order_relaxed);
            }
        }

        output = std::move(*cell->value);
        cell->value.reset();
        cell->sequence.store(position + capacity_, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

private:
    static constexpr std::size_t cache_line_size = 64;

    struct Cell {
        std::atomic<std::size_t> sequence{0};
        std::optional<T> value;
    };

    struct alignas(cache_line_size) PaddedPosition {
        std::atomic<std::size_t> value{0};
    };

    [[nodiscard]] static std::size_t checked_capacity(std::size_t capacity) {
        if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("MpmcQueue capacity must be a power of two and at least two");
        }
        return capacity;
    }

    template <typename U>
    bool emplace(U&& value) noexcept {
        std::size_t position = enqueue_position_.value.load(std::memory_order_relaxed);
        Cell* cell = nullptr;

        for (;;) {
            cell = &cells_[position & mask_];
            const std::size_t sequence = cell->sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t>(sequence) -
                                    static_cast<std::intptr_t>(position);

            if (difference == 0) {
                if (enqueue_position_.value.compare_exchange_weak(
                        position, position + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = enqueue_position_.value.load(std::memory_order_relaxed);
            }
        }

        cell->value.emplace(std::forward<U>(value));
        cell->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<Cell[]> cells_;
    PaddedPosition enqueue_position_{};
    PaddedPosition dequeue_position_{};
};

}  // namespace llq
