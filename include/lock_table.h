/**
 * @file lock_table.h
 * @brief Hash table and synchronization logic for named locks in DLock.
 *
 * Each named lock is dynamically allocated on first use and stored in a fixed
 * bucket hash table with collision chaining. Locks maintain an internal capacity
 * (1 for exclusive mutex, >1 for counting semaphore), an array of active holders,
 * a FIFO wait queue, and synchronization primitives.
 *
 * Lock Ordering Rule:
 * table_mutex -> lock_mutex -> graph_mutex -> stats/log mutex.
 */

#ifndef DLOCK_LOCK_TABLE_H
#define DLOCK_LOCK_TABLE_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "wait_queue.h"

typedef struct {
    char client_id[DLOCK_MAX_CLIENT_ID_LEN + 1];
    uint64_t token;
    uint64_t expiry_time_ms;
} lock_holder_t;

typedef struct lock_entry {
    char name[DLOCK_MAX_NAME_LEN + 1];
    uint32_t capacity;
    uint32_t num_holders;
    lock_holder_t holders[DLOCK_MAX_HOLDERS];
    wait_queue_t wait_queue;
    pthread_mutex_t lock_mutex;
    struct lock_entry *next;
} lock_entry_t;

/**
 * @brief Initialize the global lock table and its synchronization locks.
 */
void lock_table_init(void);

/**
 * @brief Set the initial fencing token value (used after WAL recovery).
 *
 * @param token Starting token value.
 */
void lock_table_set_fencing_token(uint64_t token);

/**
 * @brief Get the current fencing token value.
 *
 * @return uint64_t Current fencing token.
 */
uint64_t lock_table_get_fencing_token(void);

/**
 * @brief Create an explicit lock with a custom capacity (counting semaphore).
 *
 * @param name Lock name.
 * @param capacity Maximum concurrent holders (>0).
 * @return int DLOCK_SUCCESS on success, negative error code on failure.
 */
int lock_table_create(const char *name, uint32_t capacity);

/**
 * @brief Attempt to acquire a lock, blocking if busy until granted or timed out.
 *
 * Evaluates deadlock detection prior to sleeping.
 *
 * @param client_id Identifier of requesting client.
 * @param name Lock name.
 * @param timeout_ms Max wait duration in ms (0 = wait forever).
 * @param lease_ms Lease duration in ms (default DLOCK_DEFAULT_LEASE_MS).
 * @param out_token Pointer to store the assigned fencing token.
 * @return int DLOCK_SUCCESS, DLOCK_ERR_TIMEOUT, DLOCK_ERR_DEADLOCK, or DLOCK_ERR_BUSY.
 */
int lock_table_acquire(const char *client_id,
                       const char *name,
                       uint32_t timeout_ms,
                       uint32_t lease_ms,
                       uint64_t *out_token);

/**
 * @brief Non-blocking acquisition attempt.
 *
 * @param client_id Identifier of requesting client.
 * @param name Lock name.
 * @param lease_ms Lease duration in ms.
 * @param out_token Pointer to store the assigned fencing token.
 * @return int DLOCK_SUCCESS or DLOCK_ERR_BUSY.
 */
int lock_table_trylock(const char *client_id,
                       const char *name,
                       uint32_t lease_ms,
                       uint64_t *out_token);

/**
 * @brief Release a lock holding matching fencing token.
 *
 * @param client_id Releasing client identifier.
 * @param name Lock name.
 * @param token Fencing token returned when granted.
 * @return int DLOCK_SUCCESS, DLOCK_ERR_NOTOWNER, or DLOCK_ERR_EXPIRED.
 */
int lock_table_unlock(const char *client_id, const char *name, uint64_t token);

/**
 * @brief Extend the lease TTL of a held lock (heartbeat).
 *
 * @param client_id Holder client identifier.
 * @param name Lock name.
 * @param token Fencing token held by caller.
 * @param lease_ms New lease duration from current time.
 * @return int DLOCK_SUCCESS, DLOCK_ERR_NOTOWNER, or DLOCK_ERR_EXPIRED.
 */
int lock_table_renew(const char *client_id, const char *name, uint64_t token, uint32_t lease_ms);

/**
 * @brief Release all locks and remove from all wait queues for a client.
 *
 * Invoked when a client disconnects or crashes unexpectedly.
 *
 * @param client_id Target client identifier.
 * @return int Number of locks released or removed from.
 */
int lock_table_release_all_for_client(const char *client_id);

/**
 * @brief Scan all locks for expired leases, force release them, and wake waiters.
 *
 * @param now_ms Monotonic timestamp for current time.
 * @param next_expiry_ms Output parameter for earliest remaining lease deadline.
 * @return int Number of leases expired and freed.
 */
int lock_table_expire_leases(uint64_t now_ms, uint64_t *next_expiry_ms);

/**
 * @brief Retrieve human-readable status for a specific lock or all locks.
 *
 * @param name Lock name (or NULL / empty for all locks).
 * @param buf Output text buffer.
 * @param buf_sz Size of output buffer.
 * @return int Number of bytes formatted.
 */
int lock_table_get_status(const char *name, char *buf, size_t buf_sz);

/**
 * @brief Destroy the lock table and all allocated locks and condition variables.
 */
void lock_table_cleanup(void);

#endif /* DLOCK_LOCK_TABLE_H */
