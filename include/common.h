/**
 * @file common.h
 * @brief Global definitions, constants, and error codes for DLock.
 *
 * DLock is an in-memory, distributed-style lock server designed to coordinate
 * mutual exclusion across multiple client processes. This header centralizes
 * all capacity limits, timing defaults, network constants, and error codes.
 */

#ifndef DLOCK_COMMON_H
#define DLOCK_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Capacity & Limits */
#define DLOCK_MAX_LOCKS          1024
#define DLOCK_MAX_WAITERS        128
#define DLOCK_MAX_NAME_LEN       64
#define DLOCK_MAX_CLIENT_ID_LEN  32
#define DLOCK_MAX_LINE_LEN       256
#define DLOCK_MAX_HOLDERS        32

/* Network & Concurrency Defaults */
#define DLOCK_DEFAULT_PORT        7070
#define DLOCK_DEFAULT_WORKERS     8
#define DLOCK_DEFAULT_MAX_CLIENTS 64
#define DLOCK_DEFAULT_TIMEOUT_MS  0
#define DLOCK_DEFAULT_LEASE_MS    10000

/* Queue & Hash Table Constants */
#define DLOCK_LOCK_TABLE_BUCKETS  128
#define DLOCK_JOB_QUEUE_CAPACITY  128

/* System Paths */
#define DLOCK_DEFAULT_WAL_PATH   "data/dlock.wal"
#define DLOCK_ADMIN_FIFO_PATH    "/tmp/dlock.ctl"
#define DLOCK_DEFAULT_LOG_PATH   "logs/dlock.log"

/* Error Codes */
typedef enum {
    DLOCK_SUCCESS           =  0,
    DLOCK_ERR_TIMEOUT       = -1,
    DLOCK_ERR_BUSY          = -2,
    DLOCK_ERR_DEADLOCK      = -3,
    DLOCK_ERR_NOTOWNER      = -4,
    DLOCK_ERR_EXPIRED       = -5,
    DLOCK_ERR_BADCMD        = -6,
    DLOCK_ERR_NOHELLO       = -7,
    DLOCK_ERR_SYSTEM        = -8,
    DLOCK_ERR_DISCONNECTED  = -9,
    DLOCK_ERR_LIMIT         = -10
} dlock_status_t;

/**
 * @brief Get monotonic current timestamp in milliseconds.
 *
 * Used for tracking elapsed durations and lease calculations.
 *
 * @return uint64_t Monotonic millisecond counter.
 */
static inline uint64_t current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

/**
 * @brief Get realtime clock timestamp in milliseconds.
 *
 * Used for WAL logging and pthread_cond_timedwait absolute deadlines.
 *
 * @return uint64_t Realtime millisecond counter.
 */
static inline uint64_t current_realtime_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

/**
 * @brief Convert millisecond offset into absolute timespec for timed wait.
 *
 * @param timeout_ms Duration to wait in milliseconds.
 * @param ts Output timespec struct populated with CLOCK_REALTIME target.
 */
static inline void ms_to_timespec(uint32_t timeout_ms, struct timespec *ts) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += (time_t)(timeout_ms / 1000);
    ts->tv_nsec += (long)((timeout_ms % 1000) * 1000000L);
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

#endif /* DLOCK_COMMON_H */
