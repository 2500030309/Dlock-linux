# DLock — A Distributed Mutual-Exclusion Service

> **Operating Systems Laboratory Project | B.Tech Systems Programming in C on Ubuntu / Linux**

---

## 1. Project Title
**DLock: A Centralized Mutual-Exclusion Lock Service for Linux Systems**

## 2. Problem Statement
In multi-process computing environments, concurrent processes frequently compete for unshareable physical or logical resources (such as critical data files, shared hardware devices, or memory buffers). Without synchronization, concurrent access results in race conditions, corrupted states, and undefined program behavior.

**DLock** solves this challenge by implementing a dedicated, user-space centralized lock server in C that enforces mutual exclusion across autonomous client processes using Linux system calls, POSIX IPC (named pipes), and multi-threaded synchronization primitives.

## 3. Objective
- Build an industrial-quality, user-space lock manager adhering strictly to POSIX standards.
- Provide comprehensive, verifiable demonstrations covering all six core B.Tech Operating Systems Course Outcomes (**CO-1 through CO-6**).
- Offer practical command-line tooling, real-time process monitoring via `/proc`, and educational user-level CPU scheduling simulations (FCFS, Round Robin, Priority).

## 4. Motivation
While textbook operating systems concepts are frequently taught in isolation (e.g. studying `fork()` separately from `pipe()`, or `mmap()` separately from file systems), real-world systems software synthesizes these primitives into unified architectures. DLock bridges this gap by demonstrating how processes, address spaces, file descriptors, signals, and synchronizations operate together within a single coherent project.

---

## 5. System Architecture
```
                  Multiple Autonomous Clients
             [Client A]      [Client B]      [Client C]
                 |               |               |
                 +---------------+---------------+
                                 |
                                 | POSIX Named Pipe (FIFO)
                                 v
                     +-----------------------+
                     |      DLock Server     |
                     |  - Request Dispatcher |
                     |  - Lock Manager Core  |
                     |  - Mutex & Condition  |
                     |  - Semaphore Capacity |
                     +-----------------------+
                                 |
                                 v
                     +-----------------------+
                     |    Shared Resource    |
                     |  (File / I/O / Data)  |
                     +-----------------------+
```

---

## 6. Technologies & Tools
- **Language**: C (C99 / POSIX.1-2008 standard)
- **Compiler**: GCC with `-Wall -Wextra -pthread -O2`
- **Build System**: GNU Make
- **Target OS**: Linux (Ubuntu 20.04 / 22.04 / 24.04, WSL2)
- **Analysis Tools**: `strace`, `valgrind`, `gdb`, `/proc` virtual filesystem

---

## 7. Folder Structure
```
DLock/
├── Makefile                # Build, clean, test, and debug recipes
├── README.md               # Complete project documentation
├── .gitignore              # Ignores binaries, object files, logs, FIFOs
├── include/                # Header files with modular declarations
│   ├── dlock.h             # Core constants, utilities, and logging
│   ├── protocol.h          # Request/response packet definitions
│   ├── process.h           # CO-1 & CO-2 prototypes
│   ├── ipc.h               # CO-3 prototypes (pipes, FIFOs, signals)
│   ├── memory.h            # CO-4 prototypes (mmap, COW, page faults)
│   ├── filesystem.h        # CO-5 prototypes (inodes, I/O modes)
│   ├── concurrency.h       # CO-6 prototypes (race, mutex, cv, sem)
│   ├── scheduler.h         # User-level scheduling prototypes
│   └── monitor.h           # /proc system inspection prototypes
├── src/                    # Source code modules
│   ├── main.c              # Command router and CLI handler
│   ├── server.c            # Lock server daemon
│   ├── client.c            # Client lock/unlock CLI utility
│   ├── process.c           # Process control demonstrations
│   ├── ipc.c               # Inter-process communication
│   ├── memory.c            # Virtual memory, COW, and fault demos
│   ├── filesystem.c        # Inode, stat, and file I/O demos
│   ├── concurrency.c       # Mutex, condition var, semaphore, deadlock
│   ├── scheduler.c         # FCFS, Round Robin, Priority simulations
│   └── monitor.c           # Process table and /proc monitor
└── tests/                  # Automated test harness
    └── test_dlock.c        # 31/31 integration and syllabus test cases
```

---

## 8. How to Compile
```bash
# Standard compilation (generates bin/dlock and bin/test_dlock)
make

# Compile with debugging symbols (-g -O0) for GDB / Valgrind
make debug

# Clean build artifacts and runtime state
make clean
```

---

## 9. How to Run

### Interactive Help & Command Directory
```bash
./bin/dlock help
```

### Complete Automated Verification (31/31 Tests)
```bash
make test
```

---

## 10. Multi-Terminal Demonstration for Project Review

To demonstrate the full client-server mutual exclusion service to the evaluator:

### Terminal 1: Start Server
```bash
./bin/dlock server
```

### Terminal 2: Client A Requests and Acquires Lock
```bash
./bin/dlock client lock CLIENT_A
# Output: [SUCCESS: GRANTED] Lock GRANTED to 'CLIENT_A' (Owner PID: 1234, Lifetime Grants: 1)
```

### Terminal 3: Client B Competing for Lock
```bash
./bin/dlock client lock CLIENT_B
# Output: [WAITING] Lock held by 'CLIENT_A'. Waiting in queue for grant...
# Client B blocks safely until Client A releases the lock!
```

### Terminal 2: Client A Releases Lock
```bash
./bin/dlock client unlock CLIENT_A
# Output: [SUCCESS: RELEASED] Lock RELEASED by 'CLIENT_A'
```
*Immediately upon release, Terminal 3 receives the lock handover:*
```text
# Terminal 3 Output:
[SUCCESS: GRANTED] Lock GRANTED to queued client 'CLIENT_B' (Owner PID: 1235, Lifetime Grants: 2)
```

### Terminal 4: Inspect System and Process State
```bash
./bin/dlock monitor
```

### Terminal 2: Gracefully Shut Down Server
```bash
./bin/dlock client shutdown CLIENT_A
```

---

## 11. Individual Course Outcome (CO) Commands

| Command | Course Outcome | Description |
| :--- | :--- | :--- |
| `./bin/dlock co1` | **CO-1** | Explains the 6-layer OS abstraction and demonstrates unbuffered syscalls & `strace` |
| `./bin/dlock co2 process` | **CO-2** | Process creation via `fork()`, `execvp()`, exit codes, and controlled zombie cleanup |
| `./bin/dlock co2 groups` | **CO-2** | Process Group IDs (`setpgid`), Sessions (`setsid`), and job control |
| `./bin/dlock co3 pipe` | **CO-3** | Anonymous unidirectional pipe IPC between parent and child |
| `./bin/dlock co3 fifo` | **CO-3** | Named pipe (FIFO) communication with filesystem nodes |
| `./bin/dlock co3 signal` | **CO-3** | Asynchronous signal delivery (`SIGUSR1`) via `sigaction` and `kill` |
| `./bin/dlock co4 memory` | **CO-4** | Virtual memory segment inspection via `/proc/self/maps` and heap allocation |
| `./bin/dlock co4 cow` | **CO-4** | Demonstrates Copy-on-Write memory decoupling after `fork()` |
| `./bin/dlock co4 pagefault`| **CO-4** | Minor page fault counters observation via `getrusage()` on demand-paged memory |
| `./bin/dlock co4 errors` | **CO-4** | Safe memory error prevention (dangling pointers, leaks, Valgrind integration) |
| `./bin/dlock co5 filesystem`| **CO-5** | Inode metadata (`st_ino`, `st_mode`), directory entries, and VFS architecture |
| `./bin/dlock co5 buffered` | **CO-5** | Compares `stdio` buffered I/O against direct POSIX unbuffered system calls |
| `./bin/dlock co5 mmapfile` | **CO-5** | Memory-mapped file I/O via `mmap()`, `msync()`, and `munmap()` |
| `./bin/dlock co6 race` | **CO-6** | Demonstrates race conditions on unsynchronized multi-threaded counters |
| `./bin/dlock co6 mutex` | **CO-6** | Mutual exclusion critical section enforcement via `pthread_mutex_lock()` |
| `./bin/dlock co6 condition`| **CO-6** | Producer-consumer thread coordination via `pthread_cond_wait() / signal()` |
| `./bin/dlock co6 semaphore`| **CO-6** | POSIX counting semaphore capacity control via `sem_wait() / sem_post()` |
| `./bin/dlock co6 deadlock` | **CO-6** | The 4 Coffman conditions demonstrated safely with non-blocking trylock backoff |
| `./bin/dlock scheduler` | **SIM** | Educational CPU scheduling simulation (FCFS, Round Robin, Priority, Gantt charts) |
| `./bin/dlock monitor` | **MON** | Real-time process ecosystem inspection via Linux `/proc/<pid>/status` |

---

## 12. Verification with Standard Linux Tools

### Strace (System Call Tracing)
```bash
strace -e trace=openat,write,getpid,clone,wait4 ./bin/dlock co1
```

### Valgrind (Memory Leak & Safety Verification)
```bash
valgrind --leak-check=full ./bin/dlock co4 memory
```

---

## 13. Project Limitations
1. **User-Space Implementation**: DLock is written in user space; it does not replace the Linux kernel or write kernel drivers.
2. **Local IPC Focus**: Inter-process communication uses POSIX named pipes rather than distributed network sockets or consensus algorithms (Raft/Paxos).
3. **Educational Simulations**: Process scheduling algorithms (FCFS, RR, Priority) are modeled as educational simulations rather than kernel scheduler modifications.
4. **Non-destructive Error Demos**: Memory error demonstrations illustrate concepts without triggering kernel panic or crashing unrelated processes.

---

## 14. Final Evaluation Checklist (10/10 Defense)
- [x] Clean modular C implementation with header files and GNU Makefile
- [x] Zero compilation warnings under `-Wall -Wextra -pthread -O2`
- [x] 31/31 automated test cases passing in automated test harness
- [x] Full syllabus coverage (CO-1 through CO-6) mapped to functional code
- [x] Central DLock client-server lock service with queueing, handover, and logging
- [x] Standalone executable and test suite requiring zero external dependencies
