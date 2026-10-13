/**
 * @file stats.h
 * @brief Thread-safe runtime statistics tracking for DLock.
 *
 * Maintains atomic-like counters for all major lock server metrics:
 * granted locks, released locks, timeouts, deadlocks detected, and
 * leases expired. Thread-safety is enforced via an internal mutex.
 */

#ifndef DLOCK_STATS_H
#define DLOCK_STATS_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint64_t grants;
    uint64_t releases;
    uint64_t timeouts;
    uint64_t deadlocks;
    uint64_t expiries;
    uint64_t total_connections;
    uint32_t active_connections;
} dlock_stats_t;

/**
 * @brief Initialize the statistics subsystem and internal lock.
 */
void stats_init(void);

/**
 * @brief Increment the granted locks counter.
 */
void stats_inc_grants(void);

/**
 * @brief Increment the released locks counter.
 */
void stats_inc_releases(void);

/**
 * @brief Increment the lock request timeouts counter.
 */
void stats_inc_timeouts(void);

/**
 * @brief Increment the detected deadlocks counter.
 */
void stats_inc_deadlocks(void);

/**
 * @brief Increment the expired leases counter.
 */
void stats_inc_expiries(void);

/**
 * @brief Increment connection counters when a client connects.
 */
void stats_inc_client_connect(void);

/**
 * @brief Decrement active connections counter when a client disconnects.
 */
void stats_dec_client_disconnect(void);

/**
 * @brief Take a snapshot copy of current statistics.
 *
 * @param out_stats Pointer to destination struct.
 */
void stats_get_snapshot(dlock_stats_t *out_stats);

/**
 * @brief Dump human-readable stats report to a stream.
 *
 * @param out Output stream (e.g. stdout or file).
 */
void stats_dump(FILE *out);

/**
 * @brief Clean up the statistics subsystem and destroy mutex.
 */
void stats_cleanup(void);

#endif /* DLOCK_STATS_H */
