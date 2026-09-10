#include <llq/mpmc_queue.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_capacity_validation_and_fifo() {
    for (const std::size_t invalid : {0U, 1U, 3U, 6U}) {
        bool threw = false;
        try {
            const llq::MpmcQueue<int> queue(invalid);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "invalid capacity must be rejected");
    }

    llq::MpmcQueue<int> queue(4);
    require(queue.capacity() == 4, "reported capacity is wrong");
    require(queue.try_push(11) && queue.try_push(22) && queue.try_push(33) &&
                queue.try_push(44),
            "queue must accept capacity elements");
    require(!queue.try_push(55), "full queue must reject a push");

    int output = 0;
    for (const int expected : {11, 22, 33, 44}) {
        require(queue.try_pop(output) && output == expected, "single-threaded FIFO is wrong");
    }
    require(!queue.try_pop(output), "empty queue must reject a pop");
}

void test_move_only_values() {
    llq::MpmcQueue<std::unique_ptr<int>> queue(2);
    require(queue.try_push(std::make_unique<int>(99)), "move-only push must succeed");
    std::unique_ptr<int> output;
    require(queue.try_pop(output) && output && *output == 99,
            "move-only payload was corrupted");
}

void test_concurrent_uniqueness() {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t consumer_count = 4;
    constexpr std::size_t values_per_producer = 50'000;
    constexpr std::size_t total_values = producer_count * values_per_producer;

    llq::MpmcQueue<std::size_t> queue(1024);
    std::vector<std::atomic<unsigned int>> seen(total_values);
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> in_range{true};
    std::vector<std::jthread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        threads.emplace_back([&, producer] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            const std::size_t base = producer * values_per_producer;
            for (std::size_t offset = 0; offset < values_per_producer; ++offset) {
                const std::size_t value = base + offset;
                while (!queue.try_push(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (std::size_t consumer = 0; consumer < consumer_count; ++consumer) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::size_t value = 0;
            while (consumed.load(std::memory_order_relaxed) < total_values) {
                if (queue.try_pop(value)) {
                    if (value >= total_values) {
                        in_range.store(false, std::memory_order_relaxed);
                        consumed.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    seen[value].fetch_add(1, std::memory_order_relaxed);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    threads.clear();

    require(consumed.load(std::memory_order_relaxed) == total_values,
            "all values must be consumed");
    require(in_range.load(std::memory_order_relaxed), "all values must be in range");
    for (std::size_t value = 0; value < total_values; ++value) {
        require(seen[value].load(std::memory_order_relaxed) == 1,
                "each value must be observed exactly once");
    }
}

}  // namespace

int main() {
    try {
        test_capacity_validation_and_fifo();
        test_move_only_values();
        test_concurrent_uniqueness();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All MPMC queue tests passed\n";
    return 0;
}
