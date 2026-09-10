#include <llq/mpmc_queue.hpp>
#include <llq/mutex_queue.hpp>
#include <llq/spsc_queue.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace {

struct Options {
    std::string queue{"mutex"};
    std::size_t producers{1};
    std::size_t consumers{1};
    std::size_t operations{1'000'000};
    std::size_t capacity{1024};
    std::size_t payload_bytes{64};
    std::vector<unsigned int> cpus;
};

template <std::size_t Bytes>
struct Payload {
    static_assert(Bytes > sizeof(std::uint64_t));
    std::uint64_t id{0};
    std::array<std::byte, Bytes - sizeof(std::uint64_t)> padding{};
};

template <>
struct Payload<8> {
    std::uint64_t id{0};
};

static_assert(sizeof(Payload<8>) == 8);
static_assert(sizeof(Payload<64>) == 64);
static_assert(sizeof(Payload<256>) == 256);

struct alignas(64) ConsumerResult {
    std::uint64_t checksum{0};
    std::size_t count{0};
};

[[nodiscard]] std::size_t parse_size(std::string_view text, std::string_view option) {
    std::size_t consumed = 0;
    const auto value = std::stoull(std::string(text), &consumed);
    if (consumed != text.size() || value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("invalid value for " + std::string(option));
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::vector<unsigned int> parse_cpus(std::string_view text) {
    std::vector<unsigned int> cpus;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find(',', begin);
        const auto token = text.substr(begin, end == std::string_view::npos ? text.size() - begin
                                                                           : end - begin);
        const std::size_t cpu = parse_size(token, "--cpus");
        if (cpu > std::numeric_limits<unsigned int>::max()) {
            throw std::invalid_argument("CPU index is too large");
        }
        cpus.push_back(static_cast<unsigned int>(cpu));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return cpus;
}

void print_help() {
    std::cout
        << "Usage: queue_benchmark [options]\n"
        << "  --queue mutex|spsc|mpmc\n"
        << "  --producers N          producer threads (default 1)\n"
        << "  --consumers N          consumer threads (default 1)\n"
        << "  --operations N         operations per producer (default 1000000)\n"
        << "  --capacity N           bounded queue capacity (default 1024)\n"
        << "  --payload 8|64|256     payload size in bytes (default 64)\n"
        << "  --cpus A,B,...         pin workers in producer-then-consumer order\n";
}

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            print_help();
            std::exit(0);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value after " + std::string(argument));
        }
        const std::string_view value(argv[++index]);
        if (argument == "--queue") {
            options.queue = value;
        } else if (argument == "--producers") {
            options.producers = parse_size(value, argument);
        } else if (argument == "--consumers") {
            options.consumers = parse_size(value, argument);
        } else if (argument == "--operations") {
            options.operations = parse_size(value, argument);
        } else if (argument == "--capacity") {
            options.capacity = parse_size(value, argument);
        } else if (argument == "--payload") {
            options.payload_bytes = parse_size(value, argument);
        } else if (argument == "--cpus") {
            options.cpus = parse_cpus(value);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.producers == 0 || options.consumers == 0 || options.operations == 0) {
        throw std::invalid_argument("thread counts and operations must be greater than zero");
    }
    if (options.queue != "mutex" && options.queue != "spsc" && options.queue != "mpmc") {
        throw std::invalid_argument("queue must be mutex, spsc, or mpmc");
    }
    if (options.queue == "spsc" && (options.producers != 1 || options.consumers != 1)) {
        throw std::invalid_argument("spsc requires exactly one producer and one consumer");
    }
    const std::size_t workers = options.producers + options.consumers;
    if (!options.cpus.empty() && options.cpus.size() != workers) {
        throw std::invalid_argument("--cpus must contain one CPU per worker");
    }
    return options;
}

[[nodiscard]] bool pin_current_thread(unsigned int cpu) {
#if defined(_WIN32)
    if (cpu >= sizeof(DWORD_PTR) * 8U) {
        return false;
    }
    const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << cpu;
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
#elif defined(__linux__)
    if (cpu >= CPU_SETSIZE) {
        return false;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    static_cast<void>(cpu);
    return false;
#endif
}

template <typename Queue, typename Item>
int run_benchmark(const Options& options) {
    Queue queue(options.capacity);
    const std::size_t worker_count = options.producers + options.consumers;
    std::atomic<std::size_t> ready{0};
    std::atomic<bool> start{false};
    std::atomic<std::size_t> producers_remaining{options.producers};
    std::atomic<bool> affinity_ok{true};
    std::vector<ConsumerResult> results(options.consumers);
    std::vector<std::jthread> workers;
    workers.reserve(worker_count);

    auto prepare_worker = [&](std::size_t worker_index) {
        if (!options.cpus.empty() && !pin_current_thread(options.cpus[worker_index])) {
            affinity_ok.store(false, std::memory_order_relaxed);
        }
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    };

    for (std::size_t producer = 0; producer < options.producers; ++producer) {
        workers.emplace_back([&, producer] {
            prepare_worker(producer);
            Item item{};
            const std::uint64_t base = static_cast<std::uint64_t>(producer * options.operations);
            for (std::size_t offset = 0; offset < options.operations; ++offset) {
                item.id = base + static_cast<std::uint64_t>(offset);
                while (!queue.try_push(item)) {
                    std::this_thread::yield();
                }
            }
            producers_remaining.fetch_sub(1, std::memory_order_release);
        });
    }

    for (std::size_t consumer = 0; consumer < options.consumers; ++consumer) {
        workers.emplace_back([&, consumer] {
            prepare_worker(options.producers + consumer);
            Item item{};
            ConsumerResult local;
            for (;;) {
                if (queue.try_pop(item)) {
                    local.checksum += item.id;
                    ++local.count;
                } else if (producers_remaining.load(std::memory_order_acquire) == 0) {
                    break;
                } else {
                    std::this_thread::yield();
                }
            }
            results[consumer] = local;
        });
    }

    while (ready.load(std::memory_order_acquire) != worker_count) {
        std::this_thread::yield();
    }
    const auto begin = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    workers.clear();
    const auto end = std::chrono::steady_clock::now();

    std::size_t observed_count = 0;
    std::uint64_t observed_checksum = 0;
    for (const auto& result : results) {
        observed_count += result.count;
        observed_checksum += result.checksum;
    }

    const std::size_t expected_count = options.producers * options.operations;
    const std::uint64_t count = static_cast<std::uint64_t>(expected_count);
    const std::uint64_t expected_checksum = count * (count - 1U) / 2U;
    const bool verified = observed_count == expected_count && observed_checksum == expected_checksum;
    const double seconds = std::chrono::duration<double>(end - begin).count();
    const double operations = static_cast<double>(expected_count) * 2.0;
    const double throughput_mops = operations / seconds / 1'000'000.0;

    std::cout << "{\"queue\":\"" << options.queue << "\","
              << "\"producers\":" << options.producers << ','
              << "\"consumers\":" << options.consumers << ','
              << "\"operations_per_producer\":" << options.operations << ','
              << "\"capacity\":" << options.capacity << ','
              << "\"payload_bytes\":" << options.payload_bytes << ','
              << "\"seconds\":" << seconds << ','
              << "\"throughput_mops\":" << throughput_mops << ','
              << "\"observed_count\":" << observed_count << ','
              << "\"checksum\":" << observed_checksum << ','
              << "\"verified\":" << (verified ? "true" : "false") << ','
              << "\"affinity_requested\":" << (!options.cpus.empty() ? "true" : "false") << ','
              << "\"affinity_applied\":" << (affinity_ok.load() ? "true" : "false") << "}\n";
    return verified ? 0 : 2;
}

template <typename Item>
int dispatch_queue(const Options& options) {
    if (options.queue == "mutex") {
        return run_benchmark<llq::MutexQueue<Item>, Item>(options);
    }
    if (options.queue == "spsc") {
        return run_benchmark<llq::SpscQueue<Item>, Item>(options);
    }
    return run_benchmark<llq::MpmcQueue<Item>, Item>(options);
}

int dispatch_payload(const Options& options) {
    switch (options.payload_bytes) {
    case 8:
        return dispatch_queue<Payload<8>>(options);
    case 64:
        return dispatch_queue<Payload<64>>(options);
    case 256:
        return dispatch_queue<Payload<256>>(options);
    default:
        throw std::invalid_argument("payload must be 8, 64, or 256 bytes");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return dispatch_payload(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
