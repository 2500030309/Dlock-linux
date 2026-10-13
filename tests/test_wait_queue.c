/**
 * @file test_wait_queue.c
 * @brief Unit tests for FIFO fairness of the wait queue.
 */

#include "wait_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

int main(void) {
    printf("Running wait_queue unit tests...\n");

    wait_queue_t q;
    wait_queue_init(&q);
    TEST_ASSERT(q.count == 0, "initial count 0");
    TEST_ASSERT(wait_queue_peek(&q) == NULL, "peek empty queue");

    wait_node_t node1, node2, node3;
    memset(&node1, 0, sizeof(node1));
    memset(&node2, 0, sizeof(node2));
    memset(&node3, 0, sizeof(node3));
    strncpy(node1.client_id, "client1", sizeof(node1.client_id) - 1);
    strncpy(node2.client_id, "client2", sizeof(node2.client_id) - 1);
    strncpy(node3.client_id, "client3", sizeof(node3.client_id) - 1);

    pthread_cond_init(&node1.cond, NULL);
    pthread_cond_init(&node2.cond, NULL);
    pthread_cond_init(&node3.cond, NULL);

    TEST_ASSERT(wait_queue_enqueue(&q, &node1) == DLOCK_SUCCESS, "enqueue 1");
    TEST_ASSERT(wait_queue_enqueue(&q, &node2) == DLOCK_SUCCESS, "enqueue 2");
    TEST_ASSERT(wait_queue_enqueue(&q, &node3) == DLOCK_SUCCESS, "enqueue 3");
    TEST_ASSERT(q.count == 3, "count 3");

    TEST_ASSERT(wait_queue_contains(&q, "client2"), "contains client2");
    TEST_ASSERT(!wait_queue_contains(&q, "client4"), "not contains client4");

    /* Test FIFO order */
    wait_node_t *pop1 = wait_queue_dequeue(&q);
    TEST_ASSERT(pop1 == &node1, "FIFO first is node1");
    TEST_ASSERT(strcmp(pop1->client_id, "client1") == 0, "node1 ID");

    /* Remove specific node (node3) */
    TEST_ASSERT(wait_queue_remove(&q, &node3), "remove node3");
    TEST_ASSERT(q.count == 1, "count 1 after remove");

    wait_node_t *pop2 = wait_queue_dequeue(&q);
    TEST_ASSERT(pop2 == &node2, "remaining is node2");
    TEST_ASSERT(q.count == 0, "queue empty");

    /* Test remove by client */
    wait_queue_enqueue(&q, &node1);
    wait_queue_enqueue(&q, &node2);
    int removed = wait_queue_remove_by_client(&q, "client1");
    TEST_ASSERT(removed == 1, "removed 1 by client");
    TEST_ASSERT(q.count == 1, "1 remaining");
    TEST_ASSERT(node1.cancelled, "node1 marked cancelled");

    wait_queue_destroy(&q);
    pthread_cond_destroy(&node1.cond);
    pthread_cond_destroy(&node2.cond);
    pthread_cond_destroy(&node3.cond);

    printf("test_wait_queue: ALL PASS\n");
    return EXIT_SUCCESS;
}
