#include <llq/spsc_queue.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_capacity_and_wraparound() {
    bool rejected_zero = false;
    try {
        const llq::SpscQueue<int> invalid(0);
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    require(rejected_zero, "zero capacity must be rejected");

    llq::SpscQueue<int> queue(2);
    require(queue.capacity() == 2, "reported capacity is wrong");
    require(queue.try_push(1) && queue.try_push(2), "queue must accept capacity elements");
    require(!queue.try_push(3), "full queue must reject a push");

    int output = 0;
    require(queue.try_pop(output) && output == 1, "first FIFO value is wrong");
    require(queue.try_push(3), "wrapped push must succeed");
    require(queue.try_pop(output) && output == 2, "second FIFO value is wrong");
    require(queue.try_pop(output) && output == 3, "wrapped FIFO value is wrong");
    require(!queue.try_pop(output) && queue.empty(), "drained queue must be empty");
}

void test_move_only_values() {
    llq::SpscQueue<std::unique_ptr<int>> queue(1);
    auto input = std::make_unique<int>(7);
    require(queue.try_push(std::move(input)), "move-only push must succeed");

    std::unique_ptr<int> output;
    require(queue.try_pop(output), "move-only pop must succeed");
    require(output && *output == 7, "move-only payload was corrupted");
}

void test_concurrent_fifo() {
    constexpr std::size_t value_count = 250'000;
    llq::SpscQueue<std::size_t> queue(1024);
    std::atomic<bool> start{false};
    std::atomic<bool> correct{true};

    std::jthread producer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t value = 0; value < value_count; ++value) {
            while (!queue.try_push(value)) {
                std::this_thread::yield();
            }
        }
    });

    std::jthread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t expected = 0; expected < value_count; ++expected) {
            std::size_t output = 0;
            while (!queue.try_pop(output)) {
                std::this_thread::yield();
            }
            if (output != expected) {
                correct.store(false, std::memory_order_relaxed);
            }
        }
    });

    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    require(correct.load(std::memory_order_relaxed), "concurrent FIFO order was corrupted");
    require(queue.empty(), "queue must be empty after concurrent test");
}

}  // namespace

int main() {
    try {
        test_capacity_and_wraparound();
        test_move_only_values();
        test_concurrent_fifo();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All SPSC queue tests passed\n";
    return 0;
}
