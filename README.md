# Low-Latency Lock-Free Queues

A C++20 project exploring bounded lock-free SPSC and MPMC queues, their
correctness, and their performance relative to a mutex-protected baseline.

## Planned scope

- Bounded single-producer/single-consumer (SPSC) queue
- Bounded multi-producer/multi-consumer (MPMC) queue using per-slot sequence numbers
- Mutex-protected reference implementation
- Correctness, stress, and ThreadSanitizer tests
- Throughput and latency benchmarks with CPU affinity
- NUMA-aware benchmark orchestration and result visualization

Performance results will be published after they have been reproduced on the
target hardware.

## Status

Project scaffolding is in progress.

## License

Licensed under the MIT License. See [LICENSE](LICENSE).
