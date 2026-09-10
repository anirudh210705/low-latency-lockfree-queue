#include <llq/mutex_queue.hpp>

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

void test_rejects_zero_capacity() {
    bool threw = false;
    try {
        const llq::MutexQueue<int> queue(0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "zero capacity must be rejected");
}

void test_fifo_capacity_and_wraparound() {
    llq::MutexQueue<int> queue(3);
    require(queue.capacity() == 3, "capacity must match constructor argument");
    require(queue.empty(), "new queue must be empty");

    require(queue.try_push(10), "first push must succeed");
    require(queue.try_push(20), "second push must succeed");
    require(queue.try_push(30), "third push must succeed");
    require(queue.full(), "queue must report full");
    require(!queue.try_push(40), "push to full queue must fail");

    int value = 0;
    require(queue.try_pop(value) && value == 10, "values must leave in FIFO order");
    require(queue.try_push(40), "push after pop must wrap and succeed");
    require(queue.try_pop(value) && value == 20, "second FIFO value is wrong");
    require(queue.try_pop(value) && value == 30, "third FIFO value is wrong");
    require(queue.try_pop(value) && value == 40, "wrapped FIFO value is wrong");
    require(!queue.try_pop(value), "pop from empty queue must fail");
    require(queue.empty() && queue.size() == 0, "drained queue must be empty");
}

void test_move_only_values() {
    llq::MutexQueue<std::unique_ptr<int>> queue(1);
    auto input = std::make_unique<int>(42);

    require(queue.try_push(std::move(input)), "move-only push must succeed");
    require(input == nullptr, "successful push must consume the input value");

    std::unique_ptr<int> output;
    require(queue.try_pop(output), "move-only pop must succeed");
    require(output != nullptr && *output == 42, "move-only value must be preserved");
}

void test_concurrent_producers_and_consumers() {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t consumer_count = 4;
    constexpr std::size_t values_per_producer = 10'000;
    constexpr std::size_t total_values = producer_count * values_per_producer;

    llq::MutexQueue<std::size_t> queue(256);
    std::vector<std::atomic<unsigned int>> seen(total_values);
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::vector<std::jthread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        threads.emplace_back([&, producer] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            const std::size_t first = producer * values_per_producer;
            for (std::size_t offset = 0; offset < values_per_producer; ++offset) {
                const std::size_t value = first + offset;
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
                    seen[value].fetch_add(1, std::memory_order_relaxed);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    threads.clear();  // std::jthread destruction joins every worker.

    require(consumed.load(std::memory_order_relaxed) == total_values,
            "all produced values must be consumed");
    require(queue.empty(), "queue must be empty after the concurrent run");
    for (std::size_t value = 0; value < total_values; ++value) {
        require(seen[value].load(std::memory_order_relaxed) == 1,
                "each produced value must be observed exactly once");
    }
}

}  // namespace

int main() {
    try {
        test_rejects_zero_capacity();
        test_fifo_capacity_and_wraparound();
        test_move_only_values();
        test_concurrent_producers_and_consumers();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All mutex queue tests passed\n";
    return 0;
}
