/**
 * @file wait_queue.h
 * @brief FIFO queue implementation for waiting lock requesters.
 *
 * Guarantees strict FIFO ordering per lock to prevent starvation. Each waiter
 * maintains a dedicated condition variable on which its worker thread sleeps
 * until awakened by a lock release, timeout, or deadlock abort.
 */

#ifndef DLOCK_WAIT_QUEUE_H
#define DLOCK_WAIT_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "common.h"

typedef struct wait_node {
    char client_id[DLOCK_MAX_CLIENT_ID_LEN + 1];
    uint32_t lease_ms;
    pthread_cond_t cond;
    bool granted;
    bool timed_out;
    bool deadlocked;
    bool cancelled;
    uint64_t assigned_token;
    struct wait_node *next;
} wait_node_t;

typedef struct {
    wait_node_t *head;
    wait_node_t *tail;
    size_t count;
} wait_queue_t;

/**
 * @brief Initialize an empty FIFO wait queue.
 *
 * @param q Pointer to wait queue.
 */
void wait_queue_init(wait_queue_t *q);

/**
 * @brief Append a waiter node to the tail of the queue.
 *
 * @param q Pointer to wait queue.
 * @param node Wait node to enqueue.
 * @return int DLOCK_SUCCESS on success, DLOCK_ERR_LIMIT if queue is full.
 */
int wait_queue_enqueue(wait_queue_t *q, wait_node_t *node);

/**
 * @brief Remove and return the front waiter node.
 *
 * @param q Pointer to wait queue.
 * @return wait_node_t* Dequeued waiter, or NULL if empty.
 */
wait_node_t *wait_queue_dequeue(wait_queue_t *q);

/**
 * @brief Remove a specific node from the queue (e.g. on timeout or abort).
 *
 * @param q Pointer to wait queue.
 * @param node Node to remove.
 * @return bool True if node was found and removed, false otherwise.
 */
bool wait_queue_remove(wait_queue_t *q, wait_node_t *node);

/**
 * @brief Remove any waiters belonging to a disconnected client.
 *
 * @param q Pointer to wait queue.
 * @param client_id Client identifier to purge.
 * @return int Number of nodes removed.
 */
int wait_queue_remove_by_client(wait_queue_t *q, const char *client_id);

/**
 * @brief Check if a client is already waiting in this queue.
 *
 * @param q Pointer to wait queue.
 * @param client_id Identifier to search for.
 * @return bool True if client is queued, false otherwise.
 */
bool wait_queue_contains(const wait_queue_t *q, const char *client_id);

/**
 * @brief Peek at the head of the wait queue without removal.
 *
 * @param q Pointer to wait queue.
 * @return wait_node_t* Head node, or NULL if empty.
 */
wait_node_t *wait_queue_peek(const wait_queue_t *q);

/**
 * @brief Destroy queue and free any allocated resources (if dynamically allocated).
 *
 * @param q Pointer to wait queue.
 */
void wait_queue_destroy(wait_queue_t *q);

#endif /* DLOCK_WAIT_QUEUE_H */
