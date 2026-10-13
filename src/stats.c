/**
 * @file stats.c
 * @brief Thread-safe runtime metrics collection implementation.
 */

#include "stats.h"
#include <pthread.h>
#include <string.h>

static pthread_mutex_t s_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static dlock_stats_t s_stats = {0};

void stats_init(void) {
    pthread_mutex_lock(&s_stats_mutex);
    memset(&s_stats, 0, sizeof(s_stats));
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_inc_grants(void) {
    pthread_mutex_lock(&s_stats_mutex);
    s_stats.grants++;
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_inc_releases(void) {
    pthread_mutex_lock(&s_stats_mutex);
    s_stats.releases++;
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_inc_timeouts(void) {
    pthread_mutex_lock(&s_stats_mutex);
    s_stats.timeouts++;
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_inc_deadlocks(void) {
    pthread_mutex_lock(&s_stats_mutex);
    s_stats.deadlocks++;
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_inc_expiries(void) {
    pthread_mutex_lock(&s_stats_mutex);
    s_stats.expiries++;
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_inc_client_connect(void) {
    pthread_mutex_lock(&s_stats_mutex);
    s_stats.total_connections++;
    s_stats.active_connections++;
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_dec_client_disconnect(void) {
    pthread_mutex_lock(&s_stats_mutex);
    if (s_stats.active_connections > 0) {
        s_stats.active_connections--;
    }
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_get_snapshot(dlock_stats_t *out_stats) {
    if (out_stats == NULL) {
        return;
    }
    pthread_mutex_lock(&s_stats_mutex);
    *out_stats = s_stats;
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_dump(FILE *out) {
    if (out == NULL) {
        return;
    }
    pthread_mutex_lock(&s_stats_mutex);
    fprintf(out, "=== DLock Server Statistics ===\n");
    fprintf(out, "Active Connections : %u\n", s_stats.active_connections);
    fprintf(out, "Total Connections  : %llu\n", (unsigned long long)s_stats.total_connections);
    fprintf(out, "Locks Granted      : %llu\n", (unsigned long long)s_stats.grants);
    fprintf(out, "Locks Released     : %llu\n", (unsigned long long)s_stats.releases);
    fprintf(out, "Lock Timeouts      : %llu\n", (unsigned long long)s_stats.timeouts);
    fprintf(out, "Deadlocks Aborted  : %llu\n", (unsigned long long)s_stats.deadlocks);
    fprintf(out, "Leases Expired     : %llu\n", (unsigned long long)s_stats.expiries);
    fprintf(out, "================================\n");
    fflush(out);
    pthread_mutex_unlock(&s_stats_mutex);
}

void stats_cleanup(void) {
    /* No heap allocations to free; re-initialize to zero */
    pthread_mutex_lock(&s_stats_mutex);
    memset(&s_stats, 0, sizeof(s_stats));
    pthread_mutex_unlock(&s_stats_mutex);
}
