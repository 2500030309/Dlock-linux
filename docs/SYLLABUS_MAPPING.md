# Operating Systems Course Syllabus Mapping (CO-1 to CO-6)

This document maps each specific Course Outcome (CO) from the Operating Systems engineering curriculum directly to the source code, system calls, and mechanisms implemented in **DLock**.

---

## Comprehensive Course Outcome Mapping Table

| Course Outcome | OS Conceptual Domain | DLock Files & Modules | Key Functions & System Calls | Description & Demonstration |
| :--- | :--- | :--- | :--- | :--- |
| **CO-1** | **OS as Service Layer & Syscalls** | `src/server.c`<br>`src/wal.c`<br>`src/signals.c`<br>`src/client_api.c` | `socket()`, `setsockopt()`, `bind()`, `listen()`, `accept()`, `read()`, `write()`, `open()`, `close()`, `fsync()`, `poll()`, `pipe()` | Demonstrates transition across the user-kernel boundary via direct POSIX system calls without high-level wrappers. Includes verification via `strace -c`. |
| **CO-2** | **Processes & Process Lifecycle** | `tests/stress_clients.c`<br>`demo/demo_counter.c` | `fork()`, `waitpid()`, `wait()`, `exit()`, `getpid()` | Orchestrates concurrent child processes. Guarantees deterministic lifecycle management, status collection with `WIFEXITED`/`WEXITSTATUS`, and zero zombie processes (`waitpid` loop). |
| **CO-3** | **Inter-Process Communication & Signals** | `src/signals.c`<br>`src/admin.c` | `sigaction()`, `mkfifo()`, `pipe()`, `read()`, `write()`, `unlink()` | Demonstrates asynchronous signal handling using the async-signal-safe **Self-Pipe Trick** (`SIGINT`/`SIGTERM`/`SIGUSR1`), ignoring `SIGPIPE`, and operator IPC through a named pipe FIFO (`/tmp/dlock.ctl`). |
| **CO-4** | **Memory Management & Virtual Memory** | `src/lock_table.c`<br>`src/thread_pool.c`<br>`src/wait_queue.c` | `malloc()`, `calloc()`, `free()`, AddressSanitizer, Valgrind | Enforces strict heap ownership rules. Guarantees 0 memory leaks across dynamic hash buckets and condition variables. Validated clean via AddressSanitizer (`make asan`). |
| **CO-5** | **File Systems & Storage I/O** | `src/wal.c`<br>`src/logger.c`<br>`demo/demo_counter.c` | `open()`, `O_APPEND`, `O_CREAT`, `O_WRONLY`, `write()`, `fsync()`, `fdopen()`, `fgets()`, `fscanf()` | Contrasts low-level unbuffered file descriptor I/O and hardware persistence (`fsync()`) in the Write-Ahead Log (`dlock.wal`) against buffered standard I/O streams (`FILE*`). Treats FIFOs as filesystem nodes. |
| **CO-6** | **Concurrency, Synchronization & Deadlocks** | `src/thread_pool.c`<br>`src/lock_table.c`<br>`src/deadlock.c`<br>`src/lease.c` | `pthread_create()`, `pthread_join()`, `pthread_mutex_*`, `pthread_cond_*`, `sem_init()`, `sem_trywait()`, `sem_post()` | Implements worker thread pool with bounded queue, condition-variable FIFO waiter queueing, counting semaphore client limits, DFS wait-for cycle detection, and hierarchical lock ordering. |

---

## Deep Dive by Course Outcome

### CO-1: OS as Service Layer & Syscall Profiling
DLock is a pure user-space application interacting with the Linux kernel entirely through POSIX system calls:
- **Networking**: `socket(AF_INET, SOCK_STREAM, 0)`, `setsockopt(SO_REUSEADDR)`, `bind()`, `listen()`, `accept()`.
- **I/O Multiplexing**: `poll()` multiplexes socket activity with signal delivery pipes.
- **Syscall Profiling with `strace`**:
```bash
$ strace -c ./bin/dlockd -p 7070
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 35.12    0.001250          12       104           poll
 25.40    0.000904           8       112           write
 18.25    0.000650           7        92           read
 10.15    0.000361          18        20           fsync
  6.10    0.000217          10        21           accept
  4.98    0.000177           8        22           close
------ ----------- ----------- --------- --------- ----------------
100.00    0.003559                   371           total
```

### CO-2: Process Management & Lifecycle
- `tests/stress_clients.c` and `demo/demo_counter.c` use `fork()` to spawn $M$ isolated address spaces.
- The parent process monitors every child using `waitpid(pid, &status, 0)`:
  - Validates exit status: `WIFEXITED(status) && (WEXITSTATUS(status) == 0)`.
  - Reaps all terminating children to ensure **no zombie processes** remain in the operating system process table.

### CO-3: Inter-Process Communication (IPC) & Signal Safety
1. **Async-Signal-Safe Self-Pipe**:
   - In `src/signals.c`, `sigaction()` catches `SIGINT` and `SIGTERM`.
   - Signal handlers do **not** invoke unsafe library calls like `printf()`, `malloc()`, or `pthread_mutex_lock()`. Instead, they write a single byte `'S'` to an anonymous pipe (`pipe()`).
   - The main loop's `poll()` detects this read byte and initiates a clean shutdown sequence.
2. **Named Pipe (FIFO)**:
   - In `src/admin.c`, `mkfifo("/tmp/dlock.ctl", 0666)` provides an out-of-band IPC channel. An operator can write `STATS` or `SHUTDOWN` using standard shell redirection (`echo STATS > /tmp/dlock.ctl`).

### CO-4: Memory Management
- Dynamic allocations in `src/lock_table.c` (`calloc`), `src/thread_pool.c`, and `src/client_api.c` follow strict lifecycle ownership.
- Every allocation is matched with an explicit deallocation upon clean shutdown.
- Target `make asan` links GCC's AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) to verify zero heap buffer overflows, use-after-free, or memory leaks.

### CO-5: File System I/O & Persistence
- **Durability Guarantee**: `src/wal.c` writes lock state transitions to `data/dlock.wal`. It issues `fsync(fd)` to flush kernel page caches directly to non-volatile storage before confirming lock acquisitions.
- **Atomic Recovery**: On startup, `wal_recover()` scans existing log entries to reconstruct the highest issued 64-bit fencing token.

### CO-6: Concurrency, Synchronization & Deadlocks
1. **Thread Pool**: Producer-consumer pattern using `pthread_mutex_t` and dual condition variables (`not_empty`, `not_full`).
2. **Fairness**: `wait_queue.c` implements FIFO ordering with dedicated condition variables per waiter.
3. **Deadlock Detection**: `deadlock.c` implements dynamic cycle detection on a wait-for graph using Depth-First Search (DFS).
4. **Hierarchical Lock Ordering**:
   $$\text{s\_table\_mutex} \longrightarrow \text{entry->lock\_mutex} \longrightarrow \text{s\_graph\_mutex} \longrightarrow \text{leaf mutexes}$$
   This global ordering completely avoids AB-BA synchronization deadlocks inside the server.
