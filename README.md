# DLock — Distributed Mutual-Exclusion Service in C

> **A high-performance, fault-tolerant, POSIX-compliant lock server and distributed synchronization suite in C11.**

[![C11](https://img.shields.io/badge/Language-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![POSIX](https://img.shields.io/badge/Platform-POSIX%20%2F%20Linux-orange.svg)](https://pubs.opengroup.org/)
[![Pthreads](https://img.shields.io/badge/Concurrency-Pthreads-green.svg)](https://man7.org/linux/man-pages/man7/pthreads.7.html)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

---

## 1. Project Overview

**DLock** is an in-memory distributed locking service modeled after industry coordination systems such as **Google Chubby** and **Apache ZooKeeper**. It enables multiple client processes (distributed across independent processes or networked hosts) to coordinate mutual exclusion over TCP sockets using named locks.

### Key Capabilities:
- **Named Locks & Counting Semaphores**: Create mutexes (`capacity = 1`) or counting semaphores (`capacity = N`) dynamically.
- **Strict FIFO Fairness**: Waiters are queued and served strictly in arrival order; zero client starvation.
- **Monotonic Fencing Tokens**: Every lock grant produces a strictly increasing 64-bit fencing token to prevent zombie client corruption.
- **Leases & Automatic Crash Reclamation**: Every lock includes a Time-To-Live (TTL) lease. If a client crashes (`kill -9`) or disconnects, its locks are automatically reclaimed and granted to the next waiter.
- **Deadlock Detection (Wait-For Graph)**: Dynamically maintains a directed wait-for graph and evaluates Depth-First Search (DFS) cycle detection before any client blocks. If a cycle would occur, the request is immediately rejected with `ERR DEADLOCK`.
- **Durable Write-Ahead Log (WAL)**: Records all grants, releases, and expiries to `data/dlock.wal` with `fsync()`. Tokens survive server reboots without repeating.
- **Pthreads Worker Pool**: Fixed pool of worker threads consuming connections from a bounded queue with condition variables (`not_empty`, `not_full`).
- **Async-Signal-Safe Shutdown**: Self-pipe technique for graceful `SIGINT`/`SIGTERM` handling, ignoring `SIGPIPE`, and diagnostic dumps on `SIGUSR1`.
- **Out-of-Band Admin Channel**: Named pipe FIFO (`/tmp/dlock.ctl`) for administrative introspection (`STATS`, `DUMP`, `LOG`, `SHUTDOWN`).
- **Decentralized Ricart–Agrawala Mode (Stretch)**: Includes `bin/dlockpeer` for peer-to-peer mutual exclusion with Lamport logical clocks.

---

## 2. Project Directory Layout

```
dlock/
├── README.md                  # Comprehensive manual, build instructions, and demos
├── makefile                   # GNU Makefile supporting all targets and sanitizers
├── .gitignore                 # Build artifacts, logs, and WAL files
│
├── include/                   # Public and internal modular headers
│   ├── common.h               # Shared constants, capacity limits, error codes
│   ├── protocol.h             # Line-based ASCII protocol parser and serializer
│   ├── lock_table.h           # Hash table of named locks (mutex + condvar per lock)
│   ├── wait_queue.h           # Strict FIFO queue of waiting clients per lock
│   ├── deadlock.h             # Directed wait-for graph & DFS cycle detection
│   ├── lease.h                # Dedicated background lease expiry thread
│   ├── thread_pool.h          # Fixed Pthreads worker pool & bounded job queue
│   ├── server.h               # TCP listen/accept loop & client session logic
│   ├── client_api.h           # Synchronous C client library API
│   ├── wal.h                  # Append-only write-ahead log (fsync persistence)
│   ├── signals.h              # Async-signal-safe self-pipe & sigaction setup
│   ├── admin.h                # Named pipe (/tmp/dlock.ctl) admin channel
│   ├── stats.h                # Thread-safe performance & operational counters
│   └── logger.h               # Timestamped, thread-safe logging subsystem
│
├── src/                       # Implementation source files
│   ├── server_main.c          # Entry point for server daemon (bin/dlockd)
│   ├── server.c               # Socket bind/listen/poll accept loop
│   ├── thread_pool.c          # Producer-consumer worker pool
│   ├── protocol.c             # Defensive parser for wire protocol
│   ├── lock_table.c           # Named lock acquire, trylock, unlock, renew
│   ├── wait_queue.c           # FIFO fairness logic
│   ├── deadlock.c             # DFS cycle detection implementation
│   ├── lease.c                # Lease expiration monitor (pthread_cond_timedwait)
│   ├── wal.c                  # Durable append-only disk logging
│   ├── signals.c              # POSIX signal handlers using self-pipe
│   ├── admin.c                # Admin FIFO listener thread
│   ├── stats.c                # Thread-safe metrics collection
│   ├── logger.c               # Thread-safe formatted logging
│   ├── client_api.c           # Client library implementation
│   ├── client_main.c          # Entry point for client CLI (bin/dlockctl)
│   └── ricart.c               # Peer-to-peer Ricart–Agrawala decentralized mutex
│
├── tests/                     # Unit and stress test suites
│   ├── test_protocol.c        # Unit test: protocol parsing and edge cases
│   ├── test_lock_table.c      # Unit test: acquire, release, timeout, renew
│   ├── test_wait_queue.c      # Unit test: FIFO ordering verification
│   ├── test_deadlock.c        # Unit test: 2-party and 3-party cycle detection
│   ├── stress_clients.c       # Multiprocess fork() stress test harness
│   └── run_tests.sh           # Automated test suite runner script
│
├── demo/                      # Demonstration benchmarks and scenarios
│   ├── demo_counter.c         # Counter benchmark: Race condition vs DLock
│   ├── demo_deadlock.sh       # Script demonstrating automatic deadlock abort
│   └── demo_lease.sh          # Script demonstrating crash recovery via lease expiry
│
├── docs/                      # Architectural and syllabus documentation
│   ├── DESIGN.md              # Architecture diagram, thread model, lock ordering
│   ├── PROTOCOL.md            # Detailed wire protocol specifications
│   └── SYLLABUS_MAPPING.md    # Mapping to OS Course Outcomes (CO-1 to CO-6)
│
├── data/.gitkeep              # WAL storage directory
├── logs/.gitkeep              # Server log files directory
├── bin/                       # Generated binary executables
└── obj/                       # Generated object files (.o)
```

---

## 3. Building the Project

DLock requires standard GCC and GNU Make on POSIX/Linux (or WSL2).

```bash
# Standard compilation with full optimizations and -Wall -Wextra
make

# Compile with AddressSanitizer and UndefinedBehaviorSanitizer
make asan

# Compile with ThreadSanitizer (race condition checker)
make tsan

# Clean all build artifacts and temporary data
make clean
```

---

## 4. Running the Server & Client CLI

### 4.1 Launching the Server Daemon (`bin/dlockd`)
```bash
./bin/dlockd -p 7070 -t 8 -m 64
```
Options:
- `-p <port>` : TCP port to listen on (default: `7070`)
- `-t <workers>` : Number of pool worker threads (default: `8`)
- `-m <max>` : Maximum simultaneous connected clients (default: `64`)
- `-w <path>` : Write-ahead log path (default: `data/dlock.wal`)
- `-a <fifo>` : Admin FIFO path (default: `/tmp/dlock.ctl`)
- `-l <file>` : Log file path (default: `logs/dlock.log`)
- `-v <level>` : Log verbosity (`DEBUG`, `INFO`, `WARN`, `ERROR`)

### 4.2 Using the Client CLI (`bin/dlockctl`)

#### Single Command Execution:
```bash
# Acquire a lock named 'db_backup' with 5000ms timeout and 10000ms lease
./bin/dlockctl -p 7070 -i client1 lock db_backup 5000 10000
# Output: OK 1

# Try to acquire non-blocking
./bin/dlockctl -p 7070 -i client2 trylock db_backup
# Output: ERR BUSY

# Release the lock using the assigned fencing token
./bin/dlockctl -p 7070 -i client1 unlock db_backup 1
# Output: OK
```

#### Interactive Mode (REPL):
```bash
./bin/dlockctl -p 7070 -i operator
Connected to DLock. Enter commands (type 'help' or 'quit'):
dlock> lock orders 5000
OK 2
dlock> status orders
LOCK orders capacity=1 holders=1 waiters=0
  HOLDER client=operator token=2 ttl_ms=9640
dlock> unlock orders 2
OK
dlock> quit
```

---

## 5. Running Tests & Demonstrations

### 5.1 Comprehensive Test Suite
```bash
make test
```
Executes all unit tests (`test_protocol`, `test_wait_queue`, `test_deadlock`, `test_lock_table`) followed by a 10-process concurrent stress test verifying 100% mutual exclusion.

### 5.2 Counter Benchmark (Race Condition vs. DLock)
```bash
make demo
```
Runs 8 concurrent processes incrementing a shared counter file without locking (demonstrating lost updates), and then with DLock mutual exclusion (demonstrating exact results).

### 5.3 Deadlock Prevention Demo
```bash
bash demo/demo_deadlock.sh
```
Two clients attempt an AB-BA lock inversion. The server evaluates the wait-for graph and aborts the second request with `ERR DEADLOCK`.

### 5.4 Lease Expiry & Crash Recovery Demo
```bash
bash demo/demo_lease.sh
```
A lock holder is forcefully killed (`kill -9`). The server detects the broken session, force-releases the lease, and awards the lock to the waiting client.

---

## 6. Known Limitations & Future Work

1. **Single-Node Master**: While clients can connect over any network interface, `dlockd` runs as an active primary instance. Future work includes Raft or Multi-Paxos consensus replication across multiple server instances.
2. **Dynamic Sharding**: In this version, lock names hash into local bucket chains. Distributed partitioning (consistent hashing) across a cluster of servers can be added for massive horizontal scale.
3. **Lease Extension Jitter**: When network latency is high, clock drift between clients and server must be compensated using synchronized monotonic clocks.
