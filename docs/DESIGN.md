# DLock Architecture & System Design

## 1. System Overview

DLock is a high-performance, fault-tolerant, in-memory distributed lock service implemented in C11 for POSIX/Linux systems. It provides mutual exclusion primitives across independent processes and machines over standard TCP sockets. Modeled after foundational distributed coordination services such as Google Chubby and Apache ZooKeeper, DLock enforces strict FIFO waiter fairness, dynamic deadlock prevention, monotonic fencing tokens, and lease-based crash resilience.

```
                              +---------------------------------------+
                              |         DLock Server (dlockd)         |
                              |                                       |
                              |   +-------------------------------+   |
[ Client 1 ] --(TCP 7070)---->|   |   Main Thread (poll loop)     |   |
                              |   +---------------+---------------+   |
                              |                   |                   |
[ Client 2 ] --(TCP 7070)---->|       Bounded Job Queue (sem_t)       |
                              |                   |                   |
                              |   +---------------+---------------+   |
[ Client N ] --(TCP 7070)---->|   | Fixed Worker Pool (Pthreads)  |   |
                              |   +---------------+---------------+   |
                              |                   |                   |
                              |         +---------+---------+         |
                              |         | Hash Lock Table   |         |
                              |         | (Mutex + Condvars)|         |
                              |         +----+----+----+----+         |
                              |              |    |    |              |
                              |      +-------+    |    +-------+      |
                              |      v            v            v      |
                              |  [ Wait-For ] [ Lease ]  [ Append-Only]
                              |  [  Graph   ] [ Timer ]  [  WAL File  ]
                              |  (DFS Cycle)  (Expiry)   (fsync token)
                              +---------------------------------------+
                                     ^
                                     |  (mkfifo /tmp/dlock.ctl)
                              [ Admin Channel ]
```

---

## 2. Thread Model & Concurrency

DLock utilizes a multi-threaded architecture designed to eliminate busy-waiting, maximize throughput, and guarantee clean state isolation:

| Thread | Purpose | Synchronization Primitives |
| :--- | :--- | :--- |
| **Main / Accept Thread** | Sets up `SO_REUSEADDR`, binds TCP port, and runs a `poll()` loop multiplexing client incoming connections with the signal self-pipe. Enforces connection capacity via `sem_t`. | `poll()`, `sem_trywait()`, Self-Pipe |
| **Worker Threads (Pool)** | Fixed pool of $N$ workers (default 8) consuming accepted socket descriptors from a bounded circular buffer. Each worker manages a client session from handshake to disconnect. | `pthread_mutex_t`, `pthread_cond_t` (`not_empty`, `not_full`) |
| **Lease Monitor Thread** | Dedicated background thread tracking active lock TTL deadlines. Periodically scans the lock table and awakens exactly when the earliest lease expires. | `pthread_cond_timedwait()`, `pthread_mutex_t` |
| **Admin FIFO Thread** | Background listener reading administrative operational commands from `/tmp/dlock.ctl` without requiring network sockets. | FIFO `open(O_RDWR)`, `fgets()` |
| **Signal Handlers** | Pure async-signal-safe POSIX handlers for `SIGINT`, `SIGTERM`, `SIGUSR1`, and `SIGPIPE`. | Anonymous Pipe write (Self-Pipe Trick) |

---

## 3. Global Lock Ordering Rule

To mathematically preclude server-side deadlocks between internal subsystems, all threads strictly acquire mutexes in a predefined hierarchical order:

$$\text{Table Mutex} \longrightarrow \text{Lock Entry Mutex} \longrightarrow \text{Graph Mutex} \longrightarrow \text{WAL / Stats / Logger Mutex}$$

1. **`s_table_mutex`** (Bucket lookup & hash table structural modifications).
2. **`entry->lock_mutex`** (Per-lock state: holders array, wait queue operations).
3. **`s_graph_mutex`** (Wait-for dependency graph and cycle detection).
4. **Leaf Mutexes** (`s_wal_mutex`, `s_stats_mutex`, `s_log_mutex`) are always acquired last and never held concurrently with one another.

---

## 4. Core Data Structures & Algorithms

### 4.1 Hash Table Lock Storage
- Locks are keyed by string name (`[A-Za-z0-9_.-]{1,64}`) and mapped to fixed hash buckets using the **djb2** hash algorithm.
- Collisions are resolved through linked-list chaining.
- Locks support a configurable capacity (default 1 = exclusive mutex, $>1$ = counting semaphore).

### 4.2 Strict FIFO Fairness
- Waiters queue on a FIFO linked-list queue (`wait_queue_t`).
- Each queued waiter thread sleeps on its own per-node condition variable (`pthread_cond_t`), preventing "thundering herd" wakeups when a lock is released.
- Starvation is impossible because released slots are systematically awarded to `wait_queue.head`.

### 4.3 Fencing Tokens
- A monotonically increasing 64-bit integer counter.
- Every grant or renew operation mints a new fencing token.
- In distributed environments where network delays cause false lease expirations, servers and storage backends can verify the fencing token to reject stale client writes (solving the distributed "split-brain / zombie client" problem).
- Tokens are persisted to the Write-Ahead Log (`data/dlock.wal`) with `fsync()`. Upon restart, DLock recovers the highest token and resumes strictly above it.

### 4.4 Deadlock Detection (Wait-For Graph)
- DLock maintains a directed wait-for graph $G = (V, E)$ where nodes $V$ are client identifiers and edges $(u \to v)$ indicate that client $u$ is blocked waiting for a lock held by client $v$.
- **Algorithm**: Before enqueuing a blocking client $u$, DLock temporarily introduces edges $(u \to \text{holders})$ and executes a 3-color Depth-First Search (DFS) cycle detection.
- **Victim Selection**: If a cycle is detected, the incoming requester is designated as the victim. The edge is immediately rolled back, and the client receives `ERR DEADLOCK` without ever blocking.

### 4.5 Leases & Automatic Crash Recovery
- Every lock grant has a Time-To-Live (TTL) lease (default 10,000 ms).
- If a client process terminates abnormally (`kill -9`, segmentation fault, or network severance), the dedicated lease thread detects expiry, force-releases the held lock, logs an `EXPIRE` event, and awards the lock to the next queued FIFO waiter.
- Clients can extend active leases using periodic `RENEW` heartbeats.

---

## 5. Fault Tolerance & Persistence Design

### Write-Ahead Log (WAL)
- File path: `data/dlock.wal`.
- Opened with `open(O_WRONLY | O_APPEND | O_CREAT, 0644)`.
- Format: `<timestamp_ms> <EVENT> <lock_name> <client_id> <fencing_token>\n`.
- Every `GRANT`, `RELEASE`, and `EXPIRE` is flushed to persistent disk blocks using `fsync()` prior to replying to the client.
- **Recovery Policy**: On server reboot, DLock parses the log to recover the maximum fencing token. Held locks are **not restored** upon restart (all previous client leases are treated as expired during server downtime). This prevents orphaned state and guarantees clients must re-establish sessions.

---

## 6. Peer-to-Peer Ricart–Agrawala Architecture (Stretch Goal)

In addition to the centralized daemon, DLock includes `bin/dlockpeer`, an implementation of the decentralized **Ricart–Agrawala distributed mutual exclusion algorithm**:
- Nodes maintain Lamport logical clocks.
- Critical section requests are broadcast with `(timestamp, node_id)`.
- Conflicts are resolved deterministically: lowest timestamp wins; ties are broken by lowest node ID.
- Requires no central server and tolerates arbitrary peer latency.
