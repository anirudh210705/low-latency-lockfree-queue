# Benchmark methodology

## Metric

One item transfer consists of one successful enqueue and one successful
dequeue. Reported Mops/s counts both operations, so transferring `N` items is
`2N` queue operations. Every run also checks the item count and a deterministic
64-bit checksum; an unverified run exits unsuccessfully and must be discarded.

## Measurement protocol

1. Use a Release build with the same compiler and flags for every queue.
2. Run on an otherwise idle, dedicated machine with a fixed performance power profile.
3. Record the CPU model, sockets, NUMA nodes, OS, kernel, compiler, and commit hash.
4. Pin workers to distinct physical cores; avoid SMT siblings unless testing SMT explicitly.
5. Run at least one warm-up and five measured repetitions per configuration.
6. Report the median and the observed range, not only the best sample.
7. Compare identical payload size, capacity, producer/consumer counts, and placement.
8. Keep raw CSV and metadata alongside any generated figure.

Short smoke tests, virtual machines, WSL, and shared CI runners are correctness
checks only. Their timing results are not suitable for résumé claims.

## NUMA experiments

`run_benchmarks.py --placement same-socket` selects distinct physical cores on
one socket. `--placement cross-socket` places producers on the first socket and
consumers on the second. The script rejects cross-socket mode when sysfs does
not expose two sockets.

For a defensible cross-socket comparison, use the same binary and matrix for
both placements, confirm affinity was applied, and report the ratio of medians.
Document first-touch policy and memory placement if explicit NUMA allocation is
added later.

## Interpreting results

The mutex queue is a baseline rather than a deliberately slow straw man. Queue
capacity, payload size, backoff policy, CPU topology, cache hierarchy, and the
producer/consumer ratio can all change the result. Never transfer a number
measured on one machine to a claim about another.
