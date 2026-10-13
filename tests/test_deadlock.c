/**
 * @file test_deadlock.c
 * @brief Unit tests for wait-for graph cycle detection.
 */

#include "deadlock.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

static void test_two_party_deadlock(void) {
    deadlock_init();

    /* A holds lock 1, B holds lock 2 */
    const char *holders_a[] = {"alice"};
    const char *holders_b[] = {"bob"};

    /* Bob waits on Alice: Bob -> Alice */
    bool cycle1 = deadlock_check_and_add("bob", holders_a, 1, true);
    TEST_ASSERT(!cycle1, "no cycle when bob waits on alice");

    /* Alice now wants lock 2 held by Bob: Alice -> Bob. This creates Alice -> Bob -> Alice */
    bool cycle2 = deadlock_check_and_add("alice", holders_b, 1, false);
    TEST_ASSERT(cycle2, "cycle detected for 2-party deadlock (alice -> bob -> alice)");

    deadlock_cleanup();
}

static void test_three_party_deadlock(void) {
    deadlock_init();

    /* A holds L1, B holds L2, C holds L3 */
    const char *holders_a[] = {"alice"};
    const char *holders_b[] = {"bob"};
    const char *holders_c[] = {"charlie"};

    /* Bob waits on Alice: B -> A */
    TEST_ASSERT(!deadlock_check_and_add("bob", holders_a, 1, true), "B -> A safe");

    /* Charlie waits on Bob: C -> B -> A */
    TEST_ASSERT(!deadlock_check_and_add("charlie", holders_b, 1, true), "C -> B safe");

    /* Alice attempts to wait on Charlie: A -> C -> B -> A => Cycle! */
    bool cycle = deadlock_check_and_add("alice", holders_c, 1, false);
    TEST_ASSERT(cycle, "cycle detected for 3-party deadlock (A -> C -> B -> A)");

    deadlock_cleanup();
}

static void test_no_false_positives(void) {
    deadlock_init();

    /* Multiple clients waiting on same holder (fan-in, DAG) */
    const char *holders_a[] = {"alice"};
    TEST_ASSERT(!deadlock_check_and_add("bob", holders_a, 1, true), "bob waits on alice");
    TEST_ASSERT(!deadlock_check_and_add("charlie", holders_a, 1, true), "charlie waits on alice");
    TEST_ASSERT(!deadlock_check_and_add("dave", holders_a, 1, true), "dave waits on alice");

    /* When alice finishes, remove edges */
    deadlock_remove_client("alice");

    /* Now alice can wait on bob with no remaining edges */
    const char *holders_b[] = {"bob"};
    TEST_ASSERT(!deadlock_check_and_add("alice", holders_b, 1, true), "alice waits on bob after reset");

    deadlock_cleanup();
}

int main(void) {
    printf("Running deadlock cycle detection unit tests...\n");
    test_two_party_deadlock();
    test_three_party_deadlock();
    test_no_false_positives();
    printf("test_deadlock: ALL PASS\n");
    return EXIT_SUCCESS;
}
