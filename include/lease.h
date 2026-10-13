/**
 * @file lease.h
 * @brief Background lease and TTL expiry management thread for DLock.
 *
 * Runs a dedicated background pthread that sleeps using pthread_cond_timedwait
 * until the nearest active lease expiry. When awakened, it triggers lease
 * expiration scans on the lock table, reclaims stale locks, and signals waiting
 * clients in FIFO order.
 */

#ifndef DLOCK_LEASE_H
#define DLOCK_LEASE_H

#include <stdbool.h>

/**
 * @brief Initialize the lease subsystem condition variable and mutex.
 *
 * @return int 0 on success, -1 on error.
 */
int lease_init(void);

/**
 * @brief Spawn the background lease management thread.
 *
 * @return int 0 on success, -1 on thread creation error.
 */
int lease_start(void);

/**
 * @brief Notify the lease thread that a new lease was granted or renewed.
 *
 * Signals the condition variable so the thread recomputes the earliest deadline.
 */
void lease_notify(void);

/**
 * @brief Signal the lease thread to stop and wait for it to join.
 */
void lease_stop(void);

/**
 * @brief Clean up lease subsystem resources.
 */
void lease_cleanup(void);

#endif /* DLOCK_LEASE_H */
