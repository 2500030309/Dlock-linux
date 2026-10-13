/**
 * @file test_lock_table.c
 * @brief Unit tests for lock table operations: acquire, release, timeout, renew, capacity.
 */

#include "lock_table.h"
#include "wal.h"
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
    printf("Running lock_table unit tests...\n");

    /* Initialize with temporary WAL */
    wal_init("data/test_lock_table.wal");
    lock_table_init();

    uint64_t tok1 = 0;
    int rc = lock_table_acquire("client_a", "res1", 0, 5000, &tok1);
    TEST_ASSERT(rc == DLOCK_SUCCESS, "client_a acquires res1");
    TEST_ASSERT(tok1 > 0, "token positive");

    /* Test re-entrancy (should return ERR BUSY) */
    uint64_t tok_re = 0;
    rc = lock_table_acquire("client_a", "res1", 0, 5000, &tok_re);
    TEST_ASSERT(rc == DLOCK_ERR_BUSY, "re-entrancy rejected with BUSY");

    /* Test timeout: client_b waits 50ms for held lock */
    uint64_t tok2 = 0;
    rc = lock_table_acquire("client_b", "res1", 50, 5000, &tok2);
    TEST_ASSERT(rc == DLOCK_ERR_TIMEOUT, "client_b times out on res1");

    /* Test renew */
    rc = lock_table_renew("client_a", "res1", tok1, 8000);
    TEST_ASSERT(rc == DLOCK_SUCCESS, "client_a renews res1");

    /* Test unlock */
    rc = lock_table_unlock("client_a", "res1", tok1);
    TEST_ASSERT(rc == DLOCK_SUCCESS, "client_a unlocks res1");

    /* Test unlock again (not owner) */
    rc = lock_table_unlock("client_a", "res1", tok1);
    TEST_ASSERT(rc == DLOCK_ERR_NOTOWNER, "unlock after release returns NOTOWNER");

    /* Test counting semaphore capacity */
    rc = lock_table_create("pool_res", 2);
    TEST_ASSERT(rc == DLOCK_SUCCESS, "create pool_res with capacity 2");

    uint64_t tok_p1 = 0, tok_p2 = 0, tok_p3 = 0;
    TEST_ASSERT(lock_table_acquire("worker1", "pool_res", 0, 5000, &tok_p1) == DLOCK_SUCCESS, "worker1 acquires pool_res");
    TEST_ASSERT(lock_table_acquire("worker2", "pool_res", 0, 5000, &tok_p2) == DLOCK_SUCCESS, "worker2 acquires pool_res");
    TEST_ASSERT(lock_table_trylock("worker3", "pool_res", 5000, &tok_p3) == DLOCK_ERR_BUSY, "worker3 trylock full pool_res fails");

    lock_table_unlock("worker1", "pool_res", tok_p1);
    lock_table_unlock("worker2", "pool_res", tok_p2);

    lock_table_cleanup();
    wal_close();
    remove("data/test_lock_table.wal");

    printf("test_lock_table: ALL PASS\n");
    return EXIT_SUCCESS;
}
