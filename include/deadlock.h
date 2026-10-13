/**
 * @file deadlock.h
 * @brief Wait-for graph maintenance and DFS cycle detection for DLock.
 *
 * Implements a dynamic directed wait-for graph where nodes represent active
 * clients and directed edges (A -> B) signify that client A is blocked
 * waiting for a lock currently held by client B. Before blocking, DFS cycle
 * detection is evaluated; if a cycle would be introduced, the incoming
 * request is aborted with ERR DEADLOCK (requester is chosen as the victim).
 */

#ifndef DLOCK_DEADLOCK_H
#define DLOCK_DEADLOCK_H

#include <stdbool.h>
#include <pthread.h>
#include "common.h"

#define DLOCK_MAX_GRAPH_NODES 256

/**
 * @brief Initialize the deadlock detection subsystem and its mutex.
 */
void deadlock_init(void);

/**
 * @brief Test if adding wait edges from waiter to all holders forms a cycle.
 *
 * Runs a Depth-First Search (DFS) on the wait-for graph with temporary edges.
 * If a cycle is formed, the temporary edges are discarded and true is returned.
 * If no cycle is formed and commit is true, the edges are permanently added.
 *
 * @param waiter_id Client requesting the lock.
 * @param holder_ids Array of current lock holders' client IDs.
 * @param holder_count Number of current lock holders.
 * @param commit If true and no cycle, permanently add edges to the graph.
 * @return bool True if cycle detected, false if safe.
 */
bool deadlock_check_and_add(const char *waiter_id,
                            const char *holder_ids[],
                            size_t holder_count,
                            bool commit);

/**
 * @brief Remove edges originating from waiter_id waiting on holder_ids.
 *
 * Called when a waiter successfully acquires the lock, times out, or disconnects.
 *
 * @param waiter_id Waiting client ID.
 * @param holder_ids Array of holder client IDs.
 * @param holder_count Number of holders.
 */
void deadlock_remove_edges(const char *waiter_id,
                           const char *holder_ids[],
                           size_t holder_count);

/**
 * @brief Remove all outgoing wait edges for a waiter client.
 *
 * Called whenever a client stops waiting (lock granted, timed out, or aborted).
 *
 * @param waiter_id Waiting client identifier.
 */
void deadlock_remove_waiter(const char *waiter_id);

/**
 * @brief Remove all incoming wait edges for a holder client that released a lock.
 *
 * @param holder_id Client identifier that released a lock.
 */
void deadlock_remove_holder(const char *holder_id);

/**
 * @brief Purge all incoming and outgoing edges for a given client.
 *
 * Called upon client disconnect or session termination.
 *
 * @param client_id Target client identifier.
 */
void deadlock_remove_client(const char *client_id);

/**
 * @brief Clean up and release any resources allocated for the graph.
 */
void deadlock_cleanup(void);

#endif /* DLOCK_DEADLOCK_H */
