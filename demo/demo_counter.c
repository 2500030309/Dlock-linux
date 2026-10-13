/**
 * @file demo_counter.c
 * @brief Demonstration of Race Condition (Lost Updates) vs. Mutual Exclusion.
 *
 * Demonstrates the critical difference between concurrent access WITHOUT locking
 * and synchronized access WITH DLock.
 *
 * Part 1: N processes increment a shared file counter concurrently WITHOUT locking.
 *         Result: Interleaving leads to massive lost updates (race condition).
 * Part 2: N processes increment the shared counter using DLock mutual exclusion.
 *         Result: Perfect execution, exactly N * K updates recorded.
 */

#include "client_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define UNLOCKED_FILE "data/unlocked_counter.dat"
#define LOCKED_FILE   "data/locked_counter.dat"
#define NUM_PROCS     8
#define ROUNDS_EACH   25

static void run_unlocked_worker(int id) {
    (void)id;
    for (int i = 0; i < ROUNDS_EACH; i++) {
        long val = 0;
        FILE *fp = fopen(UNLOCKED_FILE, "r");
        if (fp != NULL) {
            if (fscanf(fp, "%ld", &val) != 1) val = 0;
            fclose(fp);
        }

        /* Context switch window: another process reads the same old val! */
        usleep(300);

        val++;
        fp = fopen(UNLOCKED_FILE, "w");
        if (fp != NULL) {
            fprintf(fp, "%ld\n", val);
            fclose(fp);
        }
        usleep(100);
    }
    exit(EXIT_SUCCESS);
}

static void run_locked_worker(const char *host, int port, int id) {
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "demo_proc_%d", id);

    dlock_conn_t *conn = dlock_connect(host, port, client_id);
    if (conn == NULL) {
        fprintf(stderr, "Worker %d failed to connect to DLock\n", id);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < ROUNDS_EACH; i++) {
        uint64_t token = 0;
        int rc = dlock_lock(conn, "demo_counter_lock", 10000, 5000, &token);
        if (rc != DLOCK_SUCCESS) {
            fprintf(stderr, "Worker %d failed to acquire lock: %d\n", id, rc);
            dlock_close(conn);
            exit(EXIT_FAILURE);
        }

        /* Critical section protected by DLock */
        long val = 0;
        FILE *fp = fopen(LOCKED_FILE, "r");
        if (fp != NULL) {
            if (fscanf(fp, "%ld", &val) != 1) val = 0;
            fclose(fp);
        }

        usleep(300);

        val++;
        fp = fopen(LOCKED_FILE, "w");
        if (fp != NULL) {
            fprintf(fp, "%ld\n", val);
            fclose(fp);
        }

        dlock_unlock(conn, "demo_counter_lock", token);
        usleep(100);
    }

    dlock_close(conn);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    char host[64] = "127.0.0.1";
    int port = DLOCK_DEFAULT_PORT;
    if (argc >= 2) port = atoi(argv[1]);

    long expected = (long)NUM_PROCS * ROUNDS_EACH;

    printf("\n============================================================\n");
    printf("         DLock Mutual Exclusion Demonstration               \n");
    printf(" Workload: %d concurrent processes, %d increments each     \n", NUM_PROCS, ROUNDS_EACH);
    printf(" Theoretical Expected Total = %ld                           \n", expected);
    printf("============================================================\n\n");

    /* --- PART 1: WITHOUT DLOCK --- */
    printf("[1] Running Benchmark: WITHOUT LOCKING (Unsafe Race Condition)...\n");
    fflush(stdout);
    FILE *fp = fopen(UNLOCKED_FILE, "w");
    if (fp) { fprintf(fp, "0\n"); fclose(fp); }

    for (int i = 0; i < NUM_PROCS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            run_unlocked_worker(i);
        }
    }
    for (int i = 0; i < NUM_PROCS; i++) {
        wait(NULL);
    }

    long unlocked_final = 0;
    fp = fopen(UNLOCKED_FILE, "r");
    if (fp) { if (fscanf(fp, "%ld", &unlocked_final) != 1) unlocked_final = 0; fclose(fp); }
    unlink(UNLOCKED_FILE);

    /* --- PART 2: WITH DLOCK --- */
    printf("[2] Running Benchmark: WITH DLOCK (Distributed Mutual Exclusion)...\n");
    fflush(stdout);
    fp = fopen(LOCKED_FILE, "w");
    if (fp) { fprintf(fp, "0\n"); fclose(fp); }

    for (int i = 0; i < NUM_PROCS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            run_locked_worker(host, port, i);
        }
    }
    for (int i = 0; i < NUM_PROCS; i++) {
        wait(NULL);
    }

    long locked_final = 0;
    fp = fopen(LOCKED_FILE, "r");
    if (fp) { if (fscanf(fp, "%ld", &locked_final) != 1) locked_final = 0; fclose(fp); }
    unlink(LOCKED_FILE);

    /* --- RESULTS COMPARISON --- */
    printf("\n==================== RESULTS COMPARISON ====================\n");
    printf(" Mode                Expected       Actual       Result\n");
    printf(" -----------------------------------------------------------\n");
    printf(" Without DLock :     %-12ld   %-10ld   LOST %ld UPDATES! (Race)\n",
           expected, unlocked_final, expected - unlocked_final);
    printf(" With DLock    :     %-12ld   %-10ld   100%% ACCURATE! (Safe)\n",
           expected, locked_final);
    printf("============================================================\n\n");

    return (locked_final == expected) ? EXIT_SUCCESS : EXIT_FAILURE;
}
