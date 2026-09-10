# Low-Latency Concurrent Queues

A C++20 study of bounded concurrent queues: an SPSC ring buffer, a
sequence-numbered bounded MPMC queue, and a mutex-protected baseline. The
repository emphasizes correctness, explicit memory ordering, reproducible
measurement, and honest reporting of hardware-dependent results.

## Implementations

- `llq::MutexQueue<T>` — simple bounded reference queue protected by one mutex
- `llq::SpscQueue<T>` — single-producer/single-consumer ring with acquire/release publication
- `llq::MpmcQueue<T>` — Vyukov-style bounded MPMC ring with per-slot generations

All queues expose `try_push`, `try_pop`, and `capacity`. Operations return
immediately when the bounded queue is full or empty. See
[the design notes](docs/design.md) for contracts, memory ordering, and progress
guarantees.

## Build and test

Requirements: CMake 3.20+, a C++20 compiler, and a threads implementation.

```bash
cmake -S . -B build -DLLQ_BUILD_TESTS=ON -DLLQ_BUILD_BENCHMARKS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

ThreadSanitizer is intended for native Linux:

```bash
cmake -S . -B build-tsan -DLLQ_ENABLE_TSAN=ON -DLLQ_BUILD_BENCHMARKS=OFF
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

WSL can build and run the normal tests, although ThreadSanitizer may fail at
startup because of WSL virtual-memory mappings. Native Ubuntu CI runs TSan on
every push.

## Benchmark

Run one integrity-checked sample:

```bash
./build/benchmarks/queue_benchmark --queue mpmc --producers 4 --consumers 4 \
  --operations 1000000 --capacity 65536 --payload 64
```

Run a repeated matrix and save CSV plus machine metadata:

```bash
python3 scripts/run_benchmarks.py --binary build/benchmarks/queue_benchmark \
  --placement same-socket
python3 -m pip install -r scripts/requirements.txt
python3 scripts/plot_results.py results/raw/benchmark-YYYYMMDD-HHMMSS.csv
```

Use `--placement cross-socket` only on a machine with at least two physical
CPU sockets. Full methodology and reporting rules are in
[docs/benchmarking.md](docs/benchmarking.md).

No headline performance claims are published until the prescribed experiments
have been repeated on named, dedicated hardware.

## License

Licensed under the MIT License. See [LICENSE](LICENSE).
