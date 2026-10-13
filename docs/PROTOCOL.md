# DLock Wire Protocol Specification

## 1. Overview

The DLock wire protocol is an ASCII text-based, newline-terminated (`\n` or `\r\n`), request-response protocol designed to operate over standard TCP sockets (default port: `7070`). 

### Protocol Rules:
1. **Line Limit**: Every command and response must be at most 256 bytes including line terminators. Lines exceeding 256 bytes are rejected with `ERR BADCMD Command line exceeds maximum length`.
2. **Defensive Validation**: Inputs are rigorously validated. Identifiers must conform to character constraints:
   - `client_id`: `[A-Za-z0-9_-]{1,32}`
   - `lock_name`: `[A-Za-z0-9_.-]{1,64}`
3. **Mandatory Handshake**: The very first command sent across any client connection MUST be `HELLO <client_id>`. Any attempt to execute lock operations prior to `HELLO` is rejected with `ERR NOHELLO`.
4. **Re-entrancy**: Locks in DLock are **non-reentrant**. An attempt by an active holder to acquire the same lock returns `ERR BUSY`.

---

## 2. Client Commands

| Command | Arguments | Description |
| :--- | :--- | :--- |
| **`HELLO`** | `<client_id>` | Registers client identity. Must be the first command on a new TCP session. |
| **`LOCK`** | `<name> <timeout_ms> [lease_ms]` | Requests exclusive/shared lock. Blocks until granted or until `timeout_ms` elapses (`0` = wait indefinitely). `lease_ms` defaults to `10000` ms if omitted. |
| **`TRYLOCK`**| `<name> [lease_ms]` | Non-blocking lock attempt. Returns immediately with token if available, or `ERR BUSY`. |
| **`UNLOCK`** | `<name> <token>` | Releases lock held by client matching the 64-bit fencing `token`. |
| **`RENEW`**  | `<name> <token> <lease_ms>` | Extends lease TTL of a held lock from the current timestamp (heartbeat). |
| **`STATUS`** | `[name]` | Queries lock state. If name is supplied, inspects single lock; otherwise dumps all locks. |
| **`CREATE`** | `<name> <capacity>` | Pre-configures a lock with capacity $N > 1$ (acting as a counting semaphore). |
| **`QUIT`**   | *(none)* | Gracefully closes the client session. Releases all held locks. |

---

## 3. Server Responses

| Response | Meaning |
| :--- | :--- |
| **`OK <token>`** | Request succeeded. Returns the 64-bit monotonic fencing token. |
| **`OK`** | Operation completed successfully (e.g. for `HELLO`, `UNLOCK`, `RENEW`, `CREATE`). |
| **`ERR TIMEOUT`** | Lock request was enqueued but wait duration exceeded `timeout_ms`. |
| **`ERR BUSY`** | Lock is currently held (returned on `TRYLOCK`, re-entrant `LOCK`, or server capacity full). |
| **`ERR DEADLOCK`** | Lock request would form a wait-for cycle. Request was aborted; requester is victim. |
| **`ERR NOTOWNER`** | Client attempted to `UNLOCK` or `RENEW` a lock it does not hold or with wrong token. |
| **`ERR EXPIRED`** | The client's lease expired prior to the operation. |
| **`ERR NOHELLO`** | Client attempted to run commands before issuing `HELLO`. |
| **`ERR BADCMD <reason>`** | Command line was malformed, invalid arguments, or unknown command. |
| **`INFO <text>`** | Line item in a multi-line response (e.g. `STATUS`). Output stream terminates with `END`. |
| **`END`** | Marks the termination of multi-line `INFO` responses. |

---

## 4. Example Transcripts

### 4.1 Basic Acquire & Release
```text
C: HELLO worker_alpha
S: OK
C: LOCK database_migration 5000 10000
S: OK 1045
C: UNLOCK database_migration 1045
S: OK
C: QUIT
```

### 4.2 Non-Blocking TryLock & Contention
```text
C: HELLO worker_beta
S: OK
C: TRYLOCK orders_queue 5000
S: OK 1046
# Another client attempts trylock on same resource
C2: HELLO worker_gamma
S: OK
C2: TRYLOCK orders_queue 5000
S: ERR BUSY
```

### 4.3 Deadlock Detection
```text
# Alice holds lock A; Bob holds lock B
C1 (Alice): LOCK resource_A 5000
S: OK 1047
C2 (Bob):   LOCK resource_B 5000
S: OK 1048

# Bob waits on Alice for resource A
C2 (Bob):   LOCK resource_A 5000
# Bob blocks...

# Alice requests resource B held by Bob -> DFS detects Alice -> Bob -> Alice cycle!
C1 (Alice): LOCK resource_B 5000
S: ERR DEADLOCK
```

### 4.4 Status Inspection
```text
C: STATUS shared_cache
S: INFO LOCK shared_cache capacity=1 holders=1 waiters=1
S: INFO   HOLDER client=worker_alpha token=1049 ttl_ms=8420
S: INFO   WAITER client=worker_beta lease_ms=10000
S: END
```

### 4.5 Counting Semaphore (`CREATE`)
```text
C: CREATE worker_slots 3
S: OK
C1: LOCK worker_slots 0
S: OK 1050
C2: LOCK worker_slots 0
S: OK 1051
C3: LOCK worker_slots 0
S: OK 1052
# 4th concurrent request will block or fail on TRYLOCK
C4: TRYLOCK worker_slots
S: ERR BUSY
```
