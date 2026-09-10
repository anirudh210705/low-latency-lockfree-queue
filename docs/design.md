# Queue design

## Shared API

Each queue is constructed with a fixed capacity and owns its storage. It is
non-copyable and non-movable. `try_push` returns false when full; `try_pop`
returns false when empty. Destruction requires that worker threads have already
stopped. Calling methods outside each queue's documented producer/consumer
contract is unsupported.

## MutexQueue

`MutexQueue` protects a vector of optional slots, head/tail cursors, and the
current size with one mutex. It accepts arbitrary positive capacities and acts
as the correctness and performance baseline. It is blocking in the progress
sense because a thread may wait to acquire the mutex, even though its API does
not wait for space or data.

## SpscQueue

`SpscQueue` uses one producer-owned tail and one consumer-owned head, placed on
separate 64-byte cache lines. One spare internal slot distinguishes full from
empty while the advertised capacity remains exact.

The producer constructs a payload and publishes the new tail with a release
store. The consumer's acquire load of tail makes that construction visible.
After moving and destroying a payload, the consumer releases the new head; the
producer acquires head before reusing the slot. Owner-side cursor loads are
relaxed because no cross-thread synchronization is needed for them.

Exactly one producer and one consumer are permitted. With non-throwing payload
operations, each queue operation has a bounded number of steps.

## MpmcQueue

`MpmcQueue` uses a power-of-two ring. Every cell contains a payload and an
atomic sequence number encoding its generation. Producers and consumers claim
monotonic tickets using relaxed compare-exchange loops. A release store to the
cell sequence publishes a produced payload or releases a consumed slot; the
corresponding acquire load observes it. The global ticket cursors do not carry
payload synchronization and therefore remain relaxed.

Payload types must be nothrow move-constructible and nothrow move-assignable.
The copy overload exists only for nothrow-copy-constructible types. This avoids
leaving a permanently reserved cell when payload construction or extraction
throws.

The algorithm is frequently described informally as lock-free. Under the
strict non-blocking progress definition, however, it is **not lock-free**: a
thread paused after claiming a ticket can prevent other threads from advancing
past that cell. It is a bounded, non-blocking API with no mutexes, but the
repository does not claim a stronger progress guarantee than the algorithm
provides.

## Lifetime and overflow

Sequence counters are unsigned and intentionally wrap. Like the original
bounded algorithm, correctness assumes that a thread is not suspended long
enough for other workers to lap its observed sequence by the counter's full
range. With 64-bit `size_t`, that is not a practical limit for these tests.
