/**
 * @file stress_clients.c
 * @brief Multiprocess stress test for DLock server.
 *
 * Spawns M client processes using fork(); each worker repeatedly acquires a
 * shared lock, reads an integer from a shared counter file, verifies no concurrent
 * holder is inside, increments the value, writes it back, and releases the lock.
 * Parent process waits for all children and verifies final count == M * K.
 */

#include "client_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEFAULT_NUM_CLIENTS 10
#define DEFAULT_ROUNDS      20
#define COUNTER_FILE        "data/stress_counter.tmp"
#define HOLDER_MARKER_FILE  "data/stress_holder.tmp"

static void run_worker(const char *host, int port, int worker_id, int rounds) {
    char client_id[DLOCK_MAX_CLIENT_ID_LEN + 1];
    snprintf(client_id, sizeof(client_id), "stress_w_%d", worker_id);

    dlock_conn_t *conn = dlock_connect(host, port, client_id);
    if (conn == NULL) {
        fprintf(stderr, "[Worker %d] Connection failed\n", worker_id);
        exit(EXIT_FAILURE);
    }

    for (int r = 0; r < rounds; r++) {
        uint64_t token = 0;
        int rc = dlock_lock(conn, "counter_lock", 10000, 10000, &token);
        if (rc != DLOCK_SUCCESS) {
            fprintf(stderr, "[Worker %d] Failed to acquire lock in round %d: %d\n", worker_id, r, rc);
            dlock_close(conn);
            exit(EXIT_FAILURE);
        }

        /* Check mutual exclusion invariant: holder marker file must not exist */
        int marker_fd = open(HOLDER_MARKER_FILE, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (marker_fd < 0) {
            fprintf(stderr, "[Worker %d] INVARIANT VIOLATION: Concurrent critical section detected!\n", worker_id);
            dlock_unlock(conn, "counter_lock", token);
            dlock_close(conn);
            exit(EXIT_FAILURE);
        }

        /* Read and increment counter from file */
        long current_val = 0;
        FILE *fp = fopen(COUNTER_FILE, "r+");
        if (fp != NULL) {
            if (fscanf(fp, "%ld", &current_val) != 1) {
                current_val = 0;
            }
            rewind(fp);
        } else {
            fp = fopen(COUNTER_FILE, "w");
        }

        current_val++;
        if (fp != NULL) {
            fprintf(fp, "%ld\n", current_val);
            fflush(fp);
            fclose(fp);
        }

        /* Small delay to expose potential race conditions */
        usleep(500);

        /* Remove marker */
        close(marker_fd);
        unlink(HOLDER_MARKER_FILE);

        /* Release lock */
        rc = dlock_unlock(conn, "counter_lock", token);
        if (rc != DLOCK_SUCCESS) {
            fprintf(stderr, "[Worker %d] Failed to release lock: %d\n", worker_id, rc);
            dlock_close(conn);
            exit(EXIT_FAILURE);
        }

        usleep(500);
    }

    dlock_close(conn);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    char host[64] = "127.0.0.1";
    int port = DLOCK_DEFAULT_PORT;
    int num_clients = DEFAULT_NUM_CLIENTS;
    int rounds = DEFAULT_ROUNDS;

    if (argc >= 2) num_clients = atoi(argv[1]);
    if (argc >= 3) rounds = atoi(argv[2]);
    if (argc >= 4) port = atoi(argv[3]);

    printf("Starting stress test: %d clients, %d rounds/client on port %d\n",
           num_clients, rounds, port);

    /* Initialize counter file to 0 */
    FILE *fp = fopen(COUNTER_FILE, "w");
    if (fp == NULL) {
        perror("Failed to create counter file");
        return EXIT_FAILURE;
    }
    fprintf(fp, "0\n");
    fclose(fp);
    unlink(HOLDER_MARKER_FILE);

    pid_t pids[64];
    for (int i = 0; i < num_clients; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork failed");
            return EXIT_FAILURE;
        } else if (pids[i] == 0) {
            run_worker(host, port, i, rounds);
        }
    }

    /* Wait for all children */
    bool all_passed = true;
    for (int i = 0; i < num_clients; i++) {
        int status = 0;
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            all_passed = false;
            fprintf(stderr, "Client %d failed with status %d\n", i, status);
        }
    }

    /* Verify final counter value */
    long final_val = 0;
    fp = fopen(COUNTER_FILE, "r");
    if (fp != NULL) {
        if (fscanf(fp, "%ld", &final_val) != 1) {
            final_val = -1;
        }
        fclose(fp);
    }
    unlink(COUNTER_FILE);
    unlink(HOLDER_MARKER_FILE);

    long expected_val = (long)num_clients * rounds;
    printf("Expected count: %ld, Final count: %ld\n", expected_val, final_val);

    if (all_passed && final_val == expected_val) {
        printf("STRESS TEST: PASS (100%% Mutual Exclusion verified)\n");
        return EXIT_SUCCESS;
    } else {
        printf("STRESS TEST: FAIL\n");
        return EXIT_FAILURE;
    }
}
