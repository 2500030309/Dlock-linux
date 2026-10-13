/**
 * @file lease.c
 * @brief Background lease expiry thread implementation.
 *
 * Runs a dedicated pthread that monitors active lock leases. The thread sleeps
 * using pthread_cond_timedwait until the earliest active lease expiration time.
 * Upon waking, it reclaims expired locks and wakes queued waiters in FIFO order.
 */

#include "lease.h"
#include "lock_table.h"
#include "logger.h"
#include "common.h"
#include <pthread.h>
#include <errno.h>

static pthread_t s_lease_thread;
static pthread_mutex_t s_lease_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_lease_cond = PTHREAD_COND_INITIALIZER;
static bool s_running = false;
static bool s_thread_spawned = false;

static void *lease_worker(void *arg) {
    (void)arg;
    logger_log(LOG_LEVEL_DEBUG, "Lease management thread started");

    pthread_mutex_lock(&s_lease_mutex);

    while (s_running) {
        uint64_t now_ms = current_time_ms();
        uint64_t next_expiry_ms = UINT64_MAX;

        /* Check for expired leases across the lock table */
        pthread_mutex_unlock(&s_lease_mutex);
        lock_table_expire_leases(now_ms, &next_expiry_ms);
        pthread_mutex_lock(&s_lease_mutex);

        if (!s_running) {
            break;
        }

        if (next_expiry_ms == UINT64_MAX) {
            /* No active leases, wait up to 1 second or until signaled */
            struct timespec ts;
            ms_to_timespec(1000, &ts);
            pthread_cond_timedwait(&s_lease_cond, &s_lease_mutex, &ts);
        } else {
            uint64_t current = current_time_ms();
            if (next_expiry_ms > current) {
                uint32_t wait_ms = (uint32_t)(next_expiry_ms - current);
                if (wait_ms > 1000) {
                    wait_ms = 1000; /* Cap sleep to 1s to remain responsive to shutdown */
                }
                struct timespec ts;
                ms_to_timespec(wait_ms, &ts);
                pthread_cond_timedwait(&s_lease_cond, &s_lease_mutex, &ts);
            }
        }
    }

    pthread_mutex_unlock(&s_lease_mutex);
    logger_log(LOG_LEVEL_DEBUG, "Lease management thread terminated");
    return NULL;
}

int lease_init(void) {
    pthread_mutex_lock(&s_lease_mutex);
    s_running = false;
    s_thread_spawned = false;
    pthread_mutex_unlock(&s_lease_mutex);
    return 0;
}

int lease_start(void) {
    pthread_mutex_lock(&s_lease_mutex);
    if (s_running) {
        pthread_mutex_unlock(&s_lease_mutex);
        return 0;
    }

    s_running = true;
    int err = pthread_create(&s_lease_thread, NULL, lease_worker, NULL);
    if (err != 0) {
        s_running = false;
        pthread_mutex_unlock(&s_lease_mutex);
        logger_log(LOG_LEVEL_ERROR, "Failed to spawn lease worker thread");
        return -1;
    }

    s_thread_spawned = true;
    pthread_mutex_unlock(&s_lease_mutex);
    return 0;
}

void lease_notify(void) {
    pthread_mutex_lock(&s_lease_mutex);
    pthread_cond_signal(&s_lease_cond);
    pthread_mutex_unlock(&s_lease_mutex);
}

void lease_stop(void) {
    pthread_mutex_lock(&s_lease_mutex);
    if (!s_running) {
        pthread_mutex_unlock(&s_lease_mutex);
        return;
    }

    s_running = false;
    pthread_cond_broadcast(&s_lease_cond);
    pthread_mutex_unlock(&s_lease_mutex);

    if (s_thread_spawned) {
        pthread_join(s_lease_thread, NULL);
        s_thread_spawned = false;
    }
}

void lease_cleanup(void) {
    lease_stop();
}
