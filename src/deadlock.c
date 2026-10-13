/**
 * @file deadlock.c
 * @brief Dynamic wait-for graph maintenance and DFS cycle detection.
 *
 * Implements a directed wait-for graph where nodes are client IDs and directed
 * edges represent wait dependencies (waiter -> holder). When a client requests
 * a lock currently held by another, DFS cycle detection checks whether introducing
 * that wait dependency would produce a cycle. If so, deadlock is detected, and
 * the incoming requester is aborted without ever waiting.
 *
 * Thread Safety:
 * All graph operations are protected by s_graph_mutex.
 */

#include "deadlock.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct {
    char id[DLOCK_MAX_CLIENT_ID_LEN + 1];
    bool in_use;
} graph_node_t;

typedef struct {
    int from_idx;
    int to_idx;
    bool in_use;
} graph_edge_t;

#define MAX_GRAPH_EDGES (DLOCK_MAX_GRAPH_NODES * 8)

static pthread_mutex_t s_graph_mutex = PTHREAD_MUTEX_INITIALIZER;
static graph_node_t s_nodes[DLOCK_MAX_GRAPH_NODES];
static graph_edge_t s_edges[MAX_GRAPH_EDGES];

void deadlock_init(void) {
    pthread_mutex_lock(&s_graph_mutex);
    memset(s_nodes, 0, sizeof(s_nodes));
    memset(s_edges, 0, sizeof(s_edges));
    pthread_mutex_unlock(&s_graph_mutex);
}

static int get_or_create_node_unlocked(const char *client_id) {
    for (int i = 0; i < DLOCK_MAX_GRAPH_NODES; i++) {
        if (s_nodes[i].in_use && strcmp(s_nodes[i].id, client_id) == 0) {
            return i;
        }
    }
    for (int i = 0; i < DLOCK_MAX_GRAPH_NODES; i++) {
        if (!s_nodes[i].in_use) {
            s_nodes[i].in_use = true;
            strncpy(s_nodes[i].id, client_id, sizeof(s_nodes[i].id) - 1);
            s_nodes[i].id[sizeof(s_nodes[i].id) - 1] = '\0';
            return i;
        }
    }
    return -1;
}

static int find_node_unlocked(const char *client_id) {
    for (int i = 0; i < DLOCK_MAX_GRAPH_NODES; i++) {
        if (s_nodes[i].in_use && strcmp(s_nodes[i].id, client_id) == 0) {
            return i;
        }
    }
    return -1;
}

static bool add_edge_unlocked(int from, int to) {
    /* Prevent duplicate edges */
    for (int i = 0; i < MAX_GRAPH_EDGES; i++) {
        if (s_edges[i].in_use && s_edges[i].from_idx == from && s_edges[i].to_idx == to) {
            return true;
        }
    }
    for (int i = 0; i < MAX_GRAPH_EDGES; i++) {
        if (!s_edges[i].in_use) {
            s_edges[i].in_use = true;
            s_edges[i].from_idx = from;
            s_edges[i].to_idx = to;
            return true;
        }
    }
    return false;
}

static void remove_edge_unlocked(int from, int to) {
    for (int i = 0; i < MAX_GRAPH_EDGES; i++) {
        if (s_edges[i].in_use && s_edges[i].from_idx == from && s_edges[i].to_idx == to) {
            s_edges[i].in_use = false;
            return;
        }
    }
}

/* DFS cycle detection helper: 0=white, 1=gray (active on recursion stack), 2=black (finished) */
static bool dfs_has_cycle(int u, int color[DLOCK_MAX_GRAPH_NODES]) {
    color[u] = 1; /* Mark as in recursion stack */

    for (int i = 0; i < MAX_GRAPH_EDGES; i++) {
        if (s_edges[i].in_use && s_edges[i].from_idx == u) {
            int v = s_edges[i].to_idx;
            if (color[v] == 1) {
                return true; /* Cycle detected */
            }
            if (color[v] == 0) {
                if (dfs_has_cycle(v, color)) {
                    return true;
                }
            }
        }
    }

    color[u] = 2; /* Mark as completed */
    return false;
}

bool deadlock_check_and_add(const char *waiter_id,
                            const char *holder_ids[],
                            size_t holder_count,
                            bool commit) {
    if (waiter_id == NULL || holder_ids == NULL || holder_count == 0) {
        return false;
    }

    pthread_mutex_lock(&s_graph_mutex);

    int waiter_idx = get_or_create_node_unlocked(waiter_id);
    if (waiter_idx < 0) {
        pthread_mutex_unlock(&s_graph_mutex);
        return false;
    }

    int added_edges = 0;
    int added_from[DLOCK_MAX_HOLDERS];
    int added_to[DLOCK_MAX_HOLDERS];

    /* Temporarily add the proposed wait edges */
    for (size_t i = 0; i < holder_count && i < DLOCK_MAX_HOLDERS; i++) {
        int h_idx = get_or_create_node_unlocked(holder_ids[i]);
        if (h_idx < 0) continue;

        if (waiter_idx == h_idx) {
            /* Client is waiting on itself */
            pthread_mutex_unlock(&s_graph_mutex);
            return true;
        }

        /* Check if edge already existed */
        bool already_there = false;
        for (int e = 0; e < MAX_GRAPH_EDGES; e++) {
            if (s_edges[e].in_use && s_edges[e].from_idx == waiter_idx && s_edges[e].to_idx == h_idx) {
                already_there = true;
                break;
            }
        }

        if (!already_there) {
            if (add_edge_unlocked(waiter_idx, h_idx)) {
                added_from[added_edges] = waiter_idx;
                added_to[added_edges] = h_idx;
                added_edges++;
            }
        }
    }

    /* Run DFS starting from all nodes in the graph */
    int color[DLOCK_MAX_GRAPH_NODES] = {0};
    bool cycle_found = false;

    /* Prioritize testing from waiter_idx */
    if (dfs_has_cycle(waiter_idx, color)) {
        cycle_found = true;
    } else {
        for (int i = 0; i < DLOCK_MAX_GRAPH_NODES; i++) {
            if (s_nodes[i].in_use && color[i] == 0) {
                if (dfs_has_cycle(i, color)) {
                    cycle_found = true;
                    break;
                }
            }
        }
    }

    if (cycle_found || !commit) {
        /* Rollback temporary edges */
        for (int e = 0; e < added_edges; e++) {
            remove_edge_unlocked(added_from[e], added_to[e]);
        }
    }

    pthread_mutex_unlock(&s_graph_mutex);
    return cycle_found;
}

void deadlock_remove_edges(const char *waiter_id,
                           const char *holder_ids[],
                           size_t holder_count) {
    if (waiter_id == NULL || holder_ids == NULL || holder_count == 0) {
        return;
    }

    pthread_mutex_lock(&s_graph_mutex);
    int waiter_idx = find_node_unlocked(waiter_id);
    if (waiter_idx >= 0) {
        for (size_t i = 0; i < holder_count; i++) {
            int holder_idx = find_node_unlocked(holder_ids[i]);
            if (holder_idx >= 0) {
                remove_edge_unlocked(waiter_idx, holder_idx);
            }
        }
    }
    pthread_mutex_unlock(&s_graph_mutex);
}

void deadlock_remove_waiter(const char *waiter_id) {
    if (waiter_id == NULL) {
        return;
    }

    pthread_mutex_lock(&s_graph_mutex);
    int waiter_idx = find_node_unlocked(waiter_id);
    if (waiter_idx >= 0) {
        for (int i = 0; i < MAX_GRAPH_EDGES; i++) {
            if (s_edges[i].in_use && s_edges[i].from_idx == waiter_idx) {
                s_edges[i].in_use = false;
            }
        }
    }
    pthread_mutex_unlock(&s_graph_mutex);
}

void deadlock_remove_holder(const char *holder_id) {
    if (holder_id == NULL) {
        return;
    }

    pthread_mutex_lock(&s_graph_mutex);
    int holder_idx = find_node_unlocked(holder_id);
    if (holder_idx >= 0) {
        for (int i = 0; i < MAX_GRAPH_EDGES; i++) {
            if (s_edges[i].in_use && s_edges[i].to_idx == holder_idx) {
                s_edges[i].in_use = false;
            }
        }
    }
    pthread_mutex_unlock(&s_graph_mutex);
}

void deadlock_remove_client(const char *client_id) {
    if (client_id == NULL) {
        return;
    }

    pthread_mutex_lock(&s_graph_mutex);
    int idx = find_node_unlocked(client_id);
    if (idx >= 0) {
        /* Remove all edges connected to this node */
        for (int i = 0; i < MAX_GRAPH_EDGES; i++) {
            if (s_edges[i].in_use && (s_edges[i].from_idx == idx || s_edges[i].to_idx == idx)) {
                s_edges[i].in_use = false;
            }
        }
        s_nodes[idx].in_use = false;
    }
    pthread_mutex_unlock(&s_graph_mutex);
}

void deadlock_cleanup(void) {
    pthread_mutex_lock(&s_graph_mutex);
    memset(s_nodes, 0, sizeof(s_nodes));
    memset(s_edges, 0, sizeof(s_edges));
    pthread_mutex_unlock(&s_graph_mutex);
}
