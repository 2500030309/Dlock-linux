/**
 * @file wait_queue.c
 * @brief Implementation of FIFO wait queue for lock requesters.
 *
 * Ensures first-in, first-out fairness among waiting clients. Waiters
 * sleep on their per-node condition variables until granted or aborted.
 */

#include "wait_queue.h"
#include <string.h>

void wait_queue_init(wait_queue_t *q) {
    if (q == NULL) {
        return;
    }
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
}

int wait_queue_enqueue(wait_queue_t *q, wait_node_t *node) {
    if (q == NULL || node == NULL) {
        return DLOCK_ERR_SYSTEM;
    }
    if (q->count >= DLOCK_MAX_WAITERS) {
        return DLOCK_ERR_LIMIT;
    }

    node->next = NULL;
    if (q->tail == NULL) {
        q->head = node;
        q->tail = node;
    } else {
        q->tail->next = node;
        q->tail = node;
    }
    q->count++;
    return DLOCK_SUCCESS;
}

wait_node_t *wait_queue_dequeue(wait_queue_t *q) {
    if (q == NULL || q->head == NULL) {
        return NULL;
    }

    wait_node_t *node = q->head;
    q->head = node->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    node->next = NULL;
    q->count--;
    return node;
}

bool wait_queue_remove(wait_queue_t *q, wait_node_t *node) {
    if (q == NULL || node == NULL || q->head == NULL) {
        return false;
    }

    wait_node_t *curr = q->head;
    wait_node_t *prev = NULL;

    while (curr != NULL) {
        if (curr == node) {
            if (prev == NULL) {
                q->head = curr->next;
            } else {
                prev->next = curr->next;
            }
            if (curr == q->tail) {
                q->tail = prev;
            }
            curr->next = NULL;
            q->count--;
            return true;
        }
        prev = curr;
        curr = curr->next;
    }
    return false;
}

int wait_queue_remove_by_client(wait_queue_t *q, const char *client_id) {
    if (q == NULL || client_id == NULL || q->head == NULL) {
        return 0;
    }

    int removed = 0;
    wait_node_t *curr = q->head;
    wait_node_t *prev = NULL;

    while (curr != NULL) {
        wait_node_t *next = curr->next;
        if (strcmp(curr->client_id, client_id) == 0) {
            if (prev == NULL) {
                q->head = next;
            } else {
                prev->next = next;
            }
            if (curr == q->tail) {
                q->tail = prev;
            }
            curr->cancelled = true;
            pthread_cond_broadcast(&curr->cond);
            curr->next = NULL;
            q->count--;
            removed++;
        } else {
            prev = curr;
        }
        curr = next;
    }
    return removed;
}

bool wait_queue_contains(const wait_queue_t *q, const char *client_id) {
    if (q == NULL || client_id == NULL) {
        return false;
    }
    const wait_node_t *curr = q->head;
    while (curr != NULL) {
        if (strcmp(curr->client_id, client_id) == 0) {
            return true;
        }
        curr = curr->next;
    }
    return false;
}

wait_node_t *wait_queue_peek(const wait_queue_t *q) {
    if (q == NULL) {
        return NULL;
    }
    return q->head;
}

void wait_queue_destroy(wait_queue_t *q) {
    if (q == NULL) {
        return;
    }
    wait_node_t *curr = q->head;
    while (curr != NULL) {
        wait_node_t *next = curr->next;
        curr->cancelled = true;
        pthread_cond_broadcast(&curr->cond);
        curr->next = NULL;
        curr = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
}
