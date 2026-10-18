# FineStructureVoxel — Archived & Retired Code Ledger

> Whenever deprecated, superseded, or duplicate code is removed from the repository, its last git commit SHA, file path, summary, replacement, and exact `git checkout` restoration command are recorded here so nothing is ever lost.

---

## Phase R2 — Deprecated Queue Headers & Legacy Queue Tests (Last Present at `09cbcb9`)

**Last commit SHA before removal:** `09cbcb9` (`Phase R0: critical bug, correctness, bridge, and scaffolding wiring sweep`)

All four legacy queue classes in `include/finevox/core/deprecated/` (and their dedicated test file `tests/test_blocking_queue.cpp`) were superseded by the unified `Queue<T>` (`include/finevox/core/queue.hpp`) and `KeyedQueue<K, D>` (`include/finevox/core/keyed_queue.hpp`) templates, which combine FIFO/priority ordering, key-based deduplication with custom merge functions, bounded capacity and backpressure (`pushBlocking`), earliest-deadline alarm wakeups, and `WakeSignal` multi-queue notification.

| Removed File Path | Last Commit SHA | What It Did | Why Retired & Replacement | Restoration Command |
|-------------------|-----------------|-------------|---------------------------|---------------------|
| `include/finevox/core/deprecated/alarm_queue.hpp` | `09cbcb9` | `AlarmQueue<T>` and `AlarmQueueWithData<K, V>`: FIFO and deduplicating queues with condition-variable alarm wakeup (had inverted `wakeTime > alarmTime_` comparison). | Superseded by `Queue<T>` (`include/finevox/core/queue.hpp`) and `KeyedQueue<K, D>` (`include/finevox/core/keyed_queue.hpp`). | `git checkout 09cbcb9 -- include/finevox/core/deprecated/alarm_queue.hpp` |
| `include/finevox/core/deprecated/blocking_queue.hpp` | `09cbcb9` | `BlockingQueue<Key>` and `BlockingQueueWithData<Key, Data>`: early thread-safe deduplicating FIFO queues without alarm or `WakeSignal` support. | Superseded by `Queue<T>` (used by `ColumnManager::saveQueue_`) and `KeyedQueue<K, D>`. | `git checkout 09cbcb9 -- include/finevox/core/deprecated/blocking_queue.hpp` |
| `include/finevox/core/deprecated/coalescing_queue.hpp` | `09cbcb9` | `CoalescingQueue<Key, Data>`: key-deduplicating queue with `MergeFn` and `WakeSignal` attachment, but no internal CV or alarm wait. | Superseded by `KeyedQueue<K, D>` (`include/finevox/core/keyed_queue.hpp`). | `git checkout 09cbcb9 -- include/finevox/core/deprecated/coalescing_queue.hpp` |
| `include/finevox/core/deprecated/coalescing_queue_impl.hpp` | `09cbcb9` | Out-of-line template method definitions included by `coalescing_queue.hpp`. | Superseded by `KeyedQueue<K, D>` (`include/finevox/core/keyed_queue.hpp`). | `git checkout 09cbcb9 -- include/finevox/core/deprecated/coalescing_queue_impl.hpp` |
| `include/finevox/core/deprecated/simple_queue.hpp` | `09cbcb9` | `SimpleQueue<T>`: basic FIFO queue with `WakeSignal` attachment, but no internal CV, alarm, or capacity support. | Superseded by `Queue<T>` (`include/finevox/core/queue.hpp`). | `git checkout 09cbcb9 -- include/finevox/core/deprecated/simple_queue.hpp` |
| `include/finevox/core/deprecated/simple_queue_impl.hpp` | `09cbcb9` | Out-of-line template method definitions included by `simple_queue.hpp`. | Superseded by `Queue<T>` (`include/finevox/core/queue.hpp`). | `git checkout 09cbcb9 -- include/finevox/core/deprecated/simple_queue_impl.hpp` |
| `tests/test_blocking_queue.cpp` | `09cbcb9` | Unit tests for `BlockingQueue`, `AlarmQueue`, `AlarmQueueWithData`, and `MeshRebuildQueue`. | Replaced by `tests/test_unified_queue.cpp`, which tests `Queue<T>`, `KeyedQueue<K, D>`, and `MeshRebuildQueue`. | `git checkout 09cbcb9 -- tests/test_blocking_queue.cpp` |

### Restore All Phase R2 Archived Files at Once

```sh
git checkout 09cbcb9 -- \
  include/finevox/core/deprecated/alarm_queue.hpp \
  include/finevox/core/deprecated/blocking_queue.hpp \
  include/finevox/core/deprecated/coalescing_queue.hpp \
  include/finevox/core/deprecated/coalescing_queue_impl.hpp \
  include/finevox/core/deprecated/simple_queue.hpp \
  include/finevox/core/deprecated/simple_queue_impl.hpp \
  tests/test_blocking_queue.cpp
```
