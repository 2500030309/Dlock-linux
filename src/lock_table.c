/**
 * @file lock_table.c
 * @brief Hash table and synchronization logic for named distributed locks.
 *
 * Implements named locks with capacity control (mutex or semaphore), FIFO
 * queueing, fencing token generation, lease deadlines, and deadlock prevention.
 *
 * Global Lock Ordering Rule:
 * table_mutex -> lock_mutex -> graph_mutex -> stats/log mutex
 */

#include "lock_table.h"
#include "deadlock.h"
#include "wal.h"
#include "stats.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static pthread_mutex_t s_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static lock_entry_t *s_buckets[DLOCK_LOCK_TABLE_BUCKETS];
static uint64_t s_fencing_token_counter = 0;
static pthread_mutex_t s_token_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Hash function for lock names (djb2 algorithm) */
static unsigned int hash_name(const char *name) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*name++)) {
        hash = ((hash << 5) + hash) + (unsigned long)c;
    }
    return (unsigned int)(hash % DLOCK_LOCK_TABLE_BUCKETS);
}

void lock_table_init(void) {
    pthread_mutex_lock(&s_table_mutex);
    for (int i = 0; i < DLOCK_LOCK_TABLE_BUCKETS; i++) {
        s_buckets[i] = NULL;
    }
    pthread_mutex_unlock(&s_table_mutex);

    deadlock_init();
    stats_init();
}

void lock_table_set_fencing_token(uint64_t token) {
    pthread_mutex_lock(&s_token_mutex);
    if (token > s_fencing_token_counter) {
        s_fencing_token_counter = token;
    }
    pthread_mutex_unlock(&s_token_mutex);
}

uint64_t lock_table_get_fencing_token(void) {
    pthread_mutex_lock(&s_token_mutex);
    uint64_t tok = s_fencing_token_counter;
    pthread_mutex_unlock(&s_token_mutex);
    return tok;
}

static uint64_t next_fencing_token(void) {
    pthread_mutex_lock(&s_token_mutex);
    s_fencing_token_counter++;
    uint64_t tok = s_fencing_token_counter;
    pthread_mutex_unlock(&s_token_mutex);
    return tok;
}

/* Must be called with s_table_mutex held */
static lock_entry_t *find_or_create_lock_unlocked(const char *name, uint32_t default_capacity) {
    unsigned int b = hash_name(name);
    lock_entry_t *entry = s_buckets[b];

    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
        entry = entry->next;
    }

    /* Create new lock entry */
    entry = (lock_entry_t *)calloc(1, sizeof(lock_entry_t));
    if (entry == NULL) {
        return NULL;
    }

    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->capacity = (default_capacity > 0) ? default_capacity : 1;
    entry->num_holders = 0;
    wait_queue_init(&entry->wait_queue);
    pthread_mutex_init(&entry->lock_mutex, NULL);

    entry->next = s_buckets[b];
    s_buckets[b] = entry;

    return entry;
}

int lock_table_create(const char *name, uint32_t capacity) {
    if (name == NULL || capacity == 0 || capacity > DLOCK_MAX_HOLDERS) {
        return DLOCK_ERR_BADCMD;
    }

    pthread_mutex_lock(&s_table_mutex);
    lock_entry_t *entry = find_or_create_lock_unlocked(name, capacity);
    if (entry == NULL) {
        pthread_mutex_unlock(&s_table_mutex);
        return DLOCK_ERR_SYSTEM;
    }

    pthread_mutex_lock(&entry->lock_mutex);
    entry->capacity = capacity;
    pthread_mutex_unlock(&entry->lock_mutex);
    pthread_mutex_unlock(&s_table_mutex);

    logger_log(LOG_LEVEL_INFO, "Lock '%s' configured with capacity %u", name, capacity);
    return DLOCK_SUCCESS;
}

/* Helper: assign lock to waiter if capacity is available. Must hold entry->lock_mutex */
static bool try_grant_to_waiter(lock_entry_t *entry) {
    if (entry->num_holders >= entry->capacity) {
        return false;
    }

    wait_node_t *w = wait_queue_dequeue(&entry->wait_queue);
    if (w == NULL) {
        return false;
    }

    uint64_t tok = next_fencing_token();
    uint64_t now = current_time_ms();
    uint32_t lease = (w->lease_ms > 0) ? w->lease_ms : DLOCK_DEFAULT_LEASE_MS;

    int idx = entry->num_holders;
    strncpy(entry->holders[idx].client_id, w->client_id, sizeof(entry->holders[idx].client_id) - 1);
    entry->holders[idx].token = tok;
    entry->holders[idx].expiry_time_ms = now + lease;
    entry->num_holders++;

    wal_log_event("GRANT", entry->name, w->client_id, tok);
    stats_inc_grants();

    deadlock_remove_waiter(w->client_id);

    w->assigned_token = tok;
    w->granted = true;
    pthread_cond_signal(&w->cond);
    return true;
}

int lock_table_acquire(const char *client_id,
                       const char *name,
                       uint32_t timeout_ms,
                       uint32_t lease_ms,
                       uint64_t *out_token) {
    if (client_id == NULL || name == NULL || out_token == NULL) {
        return DLOCK_ERR_BADCMD;
    }

    pthread_mutex_lock(&s_table_mutex);
    lock_entry_t *entry = find_or_create_lock_unlocked(name, 1);
    if (entry == NULL) {
        pthread_mutex_unlock(&s_table_mutex);
        return DLOCK_ERR_SYSTEM;
    }

    pthread_mutex_lock(&entry->lock_mutex);
    pthread_mutex_unlock(&s_table_mutex);

    /* 1. Re-entrancy check: client cannot acquire a lock it already holds */
    for (uint32_t i = 0; i < entry->num_holders; i++) {
        if (strcmp(entry->holders[i].client_id, client_id) == 0) {
            pthread_mutex_unlock(&entry->lock_mutex);
            return DLOCK_ERR_BUSY;
        }
    }

    uint64_t now = current_time_ms();
    uint32_t effective_lease = (lease_ms > 0) ? lease_ms : DLOCK_DEFAULT_LEASE_MS;

    /* 2. Fast path: slot available and no prior waiters in FIFO queue */
    if (entry->num_holders < entry->capacity && entry->wait_queue.count == 0) {
        uint64_t tok = next_fencing_token();
        int idx = entry->num_holders;
        strncpy(entry->holders[idx].client_id, client_id, sizeof(entry->holders[idx].client_id) - 1);
        entry->holders[idx].token = tok;
        entry->holders[idx].expiry_time_ms = now + effective_lease;
        entry->num_holders++;

        wal_log_event("GRANT", entry->name, client_id, tok);
        stats_inc_grants();
        *out_token = tok;

        pthread_mutex_unlock(&entry->lock_mutex);
        return DLOCK_SUCCESS;
    }

    /* 3. Deadlock Detection before blocking */
    const char *holder_ids[DLOCK_MAX_HOLDERS];
    for (uint32_t i = 0; i < entry->num_holders; i++) {
        holder_ids[i] = entry->holders[i].client_id;
    }

    bool cycle = deadlock_check_and_add(client_id, holder_ids, entry->num_holders, true);
    if (cycle) {
        logger_log(LOG_LEVEL_WARN, "Deadlock cycle detected for client '%s' on lock '%s'", client_id, name);
        stats_inc_deadlocks();
        pthread_mutex_unlock(&entry->lock_mutex);
        return DLOCK_ERR_DEADLOCK;
    }

    /* 4. Enqueue into FIFO wait queue */
    wait_node_t node;
    memset(&node, 0, sizeof(node));
    strncpy(node.client_id, client_id, sizeof(node.client_id) - 1);
    node.lease_ms = effective_lease;
    pthread_cond_init(&node.cond, NULL);

    int enq_err = wait_queue_enqueue(&entry->wait_queue, &node);
    if (enq_err != DLOCK_SUCCESS) {
        deadlock_remove_waiter(client_id);
        pthread_cond_destroy(&node.cond);
        pthread_mutex_unlock(&entry->lock_mutex);
        return enq_err;
    }

    /* 5. Sleep on condition variable until granted, timed out, or aborted */
    struct timespec deadline;
    if (timeout_ms > 0) {
        ms_to_timespec(timeout_ms, &deadline);
    }

    int wait_res = 0;
    while (!node.granted && !node.cancelled) {
        if (timeout_ms == 0) {
            wait_res = pthread_cond_wait(&node.cond, &entry->lock_mutex);
        } else {
            wait_res = pthread_cond_timedwait(&node.cond, &entry->lock_mutex, &deadline);
            if (wait_res == ETIMEDOUT && !node.granted) {
                node.timed_out = true;
                break;
            }
        }
    }

    /* 6. Post-wait processing */
    deadlock_remove_waiter(client_id);

    if (node.granted) {
        *out_token = node.assigned_token;
        pthread_cond_destroy(&node.cond);
        pthread_mutex_unlock(&entry->lock_mutex);
        return DLOCK_SUCCESS;
    }

    /* Timed out or cancelled: remove from wait queue */
    wait_queue_remove(&entry->wait_queue, &node);
    pthread_cond_destroy(&node.cond);
    stats_inc_timeouts();
    pthread_mutex_unlock(&entry->lock_mutex);

    return DLOCK_ERR_TIMEOUT;
}

int lock_table_trylock(const char *client_id,
                       const char *name,
                       uint32_t lease_ms,
                       uint64_t *out_token) {
    if (client_id == NULL || name == NULL || out_token == NULL) {
        return DLOCK_ERR_BADCMD;
    }

    pthread_mutex_lock(&s_table_mutex);
    lock_entry_t *entry = find_or_create_lock_unlocked(name, 1);
    if (entry == NULL) {
        pthread_mutex_unlock(&s_table_mutex);
        return DLOCK_ERR_SYSTEM;
    }

    pthread_mutex_lock(&entry->lock_mutex);
    pthread_mutex_unlock(&s_table_mutex);

    /* Re-entrancy check */
    for (uint32_t i = 0; i < entry->num_holders; i++) {
        if (strcmp(entry->holders[i].client_id, client_id) == 0) {
            pthread_mutex_unlock(&entry->lock_mutex);
            return DLOCK_ERR_BUSY;
        }
    }

    uint64_t now = current_time_ms();
    uint32_t effective_lease = (lease_ms > 0) ? lease_ms : DLOCK_DEFAULT_LEASE_MS;

    if (entry->num_holders < entry->capacity && entry->wait_queue.count == 0) {
        uint64_t tok = next_fencing_token();
        int idx = entry->num_holders;
        strncpy(entry->holders[idx].client_id, client_id, sizeof(entry->holders[idx].client_id) - 1);
        entry->holders[idx].token = tok;
        entry->holders[idx].expiry_time_ms = now + effective_lease;
        entry->num_holders++;

        wal_log_event("GRANT", entry->name, client_id, tok);
        stats_inc_grants();
        *out_token = tok;

        pthread_mutex_unlock(&entry->lock_mutex);
        return DLOCK_SUCCESS;
    }

    pthread_mutex_unlock(&entry->lock_mutex);
    return DLOCK_ERR_BUSY;
}

int lock_table_unlock(const char *client_id, const char *name, uint64_t token) {
    if (client_id == NULL || name == NULL || token == 0) {
        return DLOCK_ERR_BADCMD;
    }

    pthread_mutex_lock(&s_table_mutex);
    unsigned int b = hash_name(name);
    lock_entry_t *entry = s_buckets[b];
    while (entry != NULL && strcmp(entry->name, name) != 0) {
        entry = entry->next;
    }

    if (entry == NULL) {
        pthread_mutex_unlock(&s_table_mutex);
        return DLOCK_ERR_NOTOWNER;
    }

    pthread_mutex_lock(&entry->lock_mutex);
    pthread_mutex_unlock(&s_table_mutex);

    int holder_idx = -1;
    uint64_t now = current_time_ms();

    for (uint32_t i = 0; i < entry->num_holders; i++) {
        if (entry->holders[i].token == token) {
            if (strcmp(entry->holders[i].client_id, client_id) == 0) {
                holder_idx = (int)i;
            } else {
                pthread_mutex_unlock(&entry->lock_mutex);
                return DLOCK_ERR_NOTOWNER;
            }
            break;
        }
    }

    if (holder_idx < 0) {
        pthread_mutex_unlock(&entry->lock_mutex);
        return DLOCK_ERR_NOTOWNER;
    }

    /* Check if lease already expired */
    if (now > entry->holders[holder_idx].expiry_time_ms) {
        /* Remove expired holder */
        for (uint32_t j = (uint32_t)holder_idx; j + 1 < entry->num_holders; j++) {
            entry->holders[j] = entry->holders[j + 1];
        }
        entry->num_holders--;
        deadlock_remove_holder(client_id);
        try_grant_to_waiter(entry);
        pthread_mutex_unlock(&entry->lock_mutex);
        return DLOCK_ERR_EXPIRED;
    }

    /* Release lock */
    for (uint32_t j = (uint32_t)holder_idx; j + 1 < entry->num_holders; j++) {
        entry->holders[j] = entry->holders[j + 1];
    }
    entry->num_holders--;
    deadlock_remove_holder(client_id);

    wal_log_event("RELEASE", entry->name, client_id, token);
    stats_inc_releases();

    /* Wake next waiter if available */
    try_grant_to_waiter(entry);

    pthread_mutex_unlock(&entry->lock_mutex);
    return DLOCK_SUCCESS;
}

int lock_table_renew(const char *client_id, const char *name, uint64_t token, uint32_t lease_ms) {
    if (client_id == NULL || name == NULL || token == 0 || lease_ms == 0) {
        return DLOCK_ERR_BADCMD;
    }

    pthread_mutex_lock(&s_table_mutex);
    unsigned int b = hash_name(name);
    lock_entry_t *entry = s_buckets[b];
    while (entry != NULL && strcmp(entry->name, name) != 0) {
        entry = entry->next;
    }

    if (entry == NULL) {
        pthread_mutex_unlock(&s_table_mutex);
        return DLOCK_ERR_NOTOWNER;
    }

    pthread_mutex_lock(&entry->lock_mutex);
    pthread_mutex_unlock(&s_table_mutex);

    uint64_t now = current_time_ms();
    for (uint32_t i = 0; i < entry->num_holders; i++) {
        if (entry->holders[i].token == token) {
            if (strcmp(entry->holders[i].client_id, client_id) != 0) {
                pthread_mutex_unlock(&entry->lock_mutex);
                return DLOCK_ERR_NOTOWNER;
            }
            if (now > entry->holders[i].expiry_time_ms) {
                pthread_mutex_unlock(&entry->lock_mutex);
                return DLOCK_ERR_EXPIRED;
            }
            /* Extend lease */
            entry->holders[i].expiry_time_ms = now + lease_ms;
            pthread_mutex_unlock(&entry->lock_mutex);
            return DLOCK_SUCCESS;
        }
    }

    pthread_mutex_unlock(&entry->lock_mutex);
    return DLOCK_ERR_NOTOWNER;
}

int lock_table_release_all_for_client(const char *client_id) {
    if (client_id == NULL) {
        return 0;
    }

    int released_count = 0;
    pthread_mutex_lock(&s_table_mutex);

    for (int i = 0; i < DLOCK_LOCK_TABLE_BUCKETS; i++) {
        lock_entry_t *entry = s_buckets[i];
        while (entry != NULL) {
            pthread_mutex_lock(&entry->lock_mutex);

            /* 1. Remove from wait queue if waiting */
            int purged = wait_queue_remove_by_client(&entry->wait_queue, client_id);
            if (purged > 0) {
                released_count += purged;
            }

            /* 2. Release any held slots */
            for (int h = (int)entry->num_holders - 1; h >= 0; h--) {
                if (strcmp(entry->holders[h].client_id, client_id) == 0) {
                    uint64_t tok = entry->holders[h].token;
                    for (uint32_t j = (uint32_t)h; j + 1 < entry->num_holders; j++) {
                        entry->holders[j] = entry->holders[j + 1];
                    }
                    entry->num_holders--;
                    wal_log_event("RELEASE", entry->name, client_id, tok);
                    stats_inc_releases();
                    released_count++;

                    /* Wake next waiter if slot opened */
                    try_grant_to_waiter(entry);
                }
            }

            pthread_mutex_unlock(&entry->lock_mutex);
            entry = entry->next;
        }
    }

    pthread_mutex_unlock(&s_table_mutex);

    /* Purge from deadlock graph */
    deadlock_remove_client(client_id);

    return released_count;
}

int lock_table_expire_leases(uint64_t now_ms, uint64_t *next_expiry_ms) {
    int expired_count = 0;
    uint64_t min_next = UINT64_MAX;

    pthread_mutex_lock(&s_table_mutex);

    for (int i = 0; i < DLOCK_LOCK_TABLE_BUCKETS; i++) {
        lock_entry_t *entry = s_buckets[i];
        while (entry != NULL) {
            pthread_mutex_lock(&entry->lock_mutex);

            for (int h = (int)entry->num_holders - 1; h >= 0; h--) {
                if (now_ms >= entry->holders[h].expiry_time_ms) {
                    /* Expired lease */
                    char expired_client[DLOCK_MAX_CLIENT_ID_LEN + 1];
                    strncpy(expired_client, entry->holders[h].client_id, sizeof(expired_client) - 1);
                    uint64_t tok = entry->holders[h].token;

                    for (uint32_t j = (uint32_t)h; j + 1 < entry->num_holders; j++) {
                        entry->holders[j] = entry->holders[j + 1];
                    }
                    entry->num_holders--;
                    deadlock_remove_holder(expired_client);

                    wal_log_event("EXPIRE", entry->name, expired_client, tok);
                    stats_inc_expiries();
                    expired_count++;
                    logger_log(LOG_LEVEL_WARN, "Lease expired on lock '%s' held by '%s' (token %llu)",
                               entry->name, expired_client, (unsigned long long)tok);

                    try_grant_to_waiter(entry);
                } else {
                    if (entry->holders[h].expiry_time_ms < min_next) {
                        min_next = entry->holders[h].expiry_time_ms;
                    }
                }
            }

            pthread_mutex_unlock(&entry->lock_mutex);
            entry = entry->next;
        }
    }

    pthread_mutex_unlock(&s_table_mutex);

    if (next_expiry_ms != NULL) {
        *next_expiry_ms = min_next;
    }
    return expired_count;
}

int lock_table_get_status(const char *name, char *buf, size_t buf_sz) {
    if (buf == NULL || buf_sz == 0) {
        return 0;
    }

    size_t offset = 0;
    uint64_t now = current_time_ms();

    pthread_mutex_lock(&s_table_mutex);

    for (int i = 0; i < DLOCK_LOCK_TABLE_BUCKETS; i++) {
        lock_entry_t *entry = s_buckets[i];
        while (entry != NULL) {
            if (name != NULL && name[0] != '\0' && strcmp(entry->name, name) != 0) {
                entry = entry->next;
                continue;
            }

            pthread_mutex_lock(&entry->lock_mutex);
            int written = snprintf(buf + offset, buf_sz - offset,
                                   "LOCK %s capacity=%u holders=%u waiters=%zu\n",
                                   entry->name, entry->capacity, entry->num_holders, entry->wait_queue.count);
            if (written > 0 && (size_t)written < buf_sz - offset) {
                offset += (size_t)written;
            }

            for (uint32_t h = 0; h < entry->num_holders; h++) {
                int64_t ttl_ms = (int64_t)entry->holders[h].expiry_time_ms - (int64_t)now;
                if (ttl_ms < 0) ttl_ms = 0;
                written = snprintf(buf + offset, buf_sz - offset,
                                   "  HOLDER client=%s token=%llu ttl_ms=%lld\n",
                                   entry->holders[h].client_id,
                                   (unsigned long long)entry->holders[h].token,
                                   (long long)ttl_ms);
                if (written > 0 && (size_t)written < buf_sz - offset) {
                    offset += (size_t)written;
                }
            }

            wait_node_t *w = entry->wait_queue.head;
            while (w != NULL) {
                written = snprintf(buf + offset, buf_sz - offset,
                                   "  WAITER client=%s lease_ms=%u\n",
                                   w->client_id, w->lease_ms);
                if (written > 0 && (size_t)written < buf_sz - offset) {
                    offset += (size_t)written;
                }
                w = w->next;
            }

            pthread_mutex_unlock(&entry->lock_mutex);
            entry = entry->next;
        }
    }

    pthread_mutex_unlock(&s_table_mutex);
    return (int)offset;
}

void lock_table_cleanup(void) {
    pthread_mutex_lock(&s_table_mutex);

    for (int i = 0; i < DLOCK_LOCK_TABLE_BUCKETS; i++) {
        lock_entry_t *entry = s_buckets[i];
        while (entry != NULL) {
            lock_entry_t *next = entry->next;
            pthread_mutex_lock(&entry->lock_mutex);
            wait_queue_destroy(&entry->wait_queue);
            pthread_mutex_unlock(&entry->lock_mutex);
            pthread_mutex_destroy(&entry->lock_mutex);
            free(entry);
            entry = next;
        }
        s_buckets[i] = NULL;
    }

    pthread_mutex_unlock(&s_table_mutex);
    deadlock_cleanup();
    stats_cleanup();
}
