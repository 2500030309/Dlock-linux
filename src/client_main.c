/**
 * @file client_main.c
 * @brief Command-line interface utility for DLock (bin/dlockctl).
 *
 * Provides single-command execution and an interactive command loop for
 * testing, operations, and scripting with the DLock server.
 */

#include "client_api.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *prog) {
    printf("Usage: %s [options] [command [arguments...]]\n", prog);
    printf("Options:\n");
    printf("  -h <host>         Server host (default: 127.0.0.1)\n");
    printf("  -p <port>         Server port (default: 7070)\n");
    printf("  -i <client_id>    Unique client ID (default: client_<pid>)\n");
    printf("\nCommands:\n");
    printf("  lock <name> [timeout_ms] [lease_ms]\n");
    printf("  trylock <name> [lease_ms]\n");
    printf("  unlock <name> <token>\n");
    printf("  renew <name> <token> <lease_ms>\n");
    printf("  status [name]\n");
    printf("  create <name> <capacity>\n");
    printf("\nIf no command is specified, %s enters interactive mode.\n", prog);
}

static int execute_single_command(dlock_conn_t *conn, int argc, char *argv[]) {
    const char *cmd = argv[0];

    if (strcasecmp(cmd, "lock") == 0) {
        if (argc < 2) {
            fprintf(stderr, "Error: Missing lock name. Usage: lock <name> [timeout_ms] [lease_ms]\n");
            return EXIT_FAILURE;
        }
        const char *name = argv[1];
        uint32_t timeout_ms = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 0;
        uint32_t lease_ms = (argc >= 4) ? (uint32_t)atoi(argv[3]) : DLOCK_DEFAULT_LEASE_MS;
        uint64_t token = 0;

        int rc = dlock_lock(conn, name, timeout_ms, lease_ms, &token);
        if (rc == DLOCK_SUCCESS) {
            printf("OK %llu\n", (unsigned long long)token);
            return EXIT_SUCCESS;
        } else if (rc == DLOCK_ERR_TIMEOUT) {
            printf("ERR TIMEOUT\n");
        } else if (rc == DLOCK_ERR_DEADLOCK) {
            printf("ERR DEADLOCK\n");
        } else if (rc == DLOCK_ERR_BUSY) {
            printf("ERR BUSY\n");
        } else {
            printf("ERR %d\n", rc);
        }
        return EXIT_FAILURE;
    }

    if (strcasecmp(cmd, "trylock") == 0) {
        if (argc < 2) {
            fprintf(stderr, "Error: Missing lock name. Usage: trylock <name> [lease_ms]\n");
            return EXIT_FAILURE;
        }
        const char *name = argv[1];
        uint32_t lease_ms = (argc >= 3) ? (uint32_t)atoi(argv[2]) : DLOCK_DEFAULT_LEASE_MS;
        uint64_t token = 0;

        int rc = dlock_trylock(conn, name, lease_ms, &token);
        if (rc == DLOCK_SUCCESS) {
            printf("OK %llu\n", (unsigned long long)token);
            return EXIT_SUCCESS;
        } else if (rc == DLOCK_ERR_BUSY) {
            printf("ERR BUSY\n");
        } else {
            printf("ERR %d\n", rc);
        }
        return EXIT_FAILURE;
    }

    if (strcasecmp(cmd, "unlock") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Usage: unlock <name> <token>\n");
            return EXIT_FAILURE;
        }
        const char *name = argv[1];
        uint64_t token = (uint64_t)strtoull(argv[2], NULL, 10);

        int rc = dlock_unlock(conn, name, token);
        if (rc == DLOCK_SUCCESS) {
            printf("OK\n");
            return EXIT_SUCCESS;
        } else if (rc == DLOCK_ERR_EXPIRED) {
            printf("ERR EXPIRED\n");
        } else {
            printf("ERR NOTOWNER\n");
        }
        return EXIT_FAILURE;
    }

    if (strcasecmp(cmd, "renew") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: Usage: renew <name> <token> <lease_ms>\n");
            return EXIT_FAILURE;
        }
        const char *name = argv[1];
        uint64_t token = (uint64_t)strtoull(argv[2], NULL, 10);
        uint32_t lease_ms = (uint32_t)atoi(argv[3]);

        int rc = dlock_renew(conn, name, token, lease_ms);
        if (rc == DLOCK_SUCCESS) {
            printf("OK\n");
            return EXIT_SUCCESS;
        } else if (rc == DLOCK_ERR_EXPIRED) {
            printf("ERR EXPIRED\n");
        } else {
            printf("ERR NOTOWNER\n");
        }
        return EXIT_FAILURE;
    }

    if (strcasecmp(cmd, "status") == 0) {
        const char *name = (argc >= 2) ? argv[1] : NULL;
        char buf[8192] = {0};
        int rc = dlock_status(conn, name, buf, sizeof(buf));
        if (rc == DLOCK_SUCCESS) {
            printf("%s", buf);
            return EXIT_SUCCESS;
        } else {
            printf("ERR STATUS\n");
            return EXIT_FAILURE;
        }
    }

    if (strcasecmp(cmd, "create") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Usage: create <name> <capacity>\n");
            return EXIT_FAILURE;
        }
        const char *name = argv[1];
        uint32_t capacity = (uint32_t)atoi(argv[2]);

        int rc = dlock_create(conn, name, capacity);
        if (rc == DLOCK_SUCCESS) {
            printf("OK\n");
            return EXIT_SUCCESS;
        } else {
            printf("ERR CREATE\n");
            return EXIT_FAILURE;
        }
    }

    fprintf(stderr, "Unknown command '%s'\n", cmd);
    return EXIT_FAILURE;
}

static void run_interactive(dlock_conn_t *conn) {
    printf("Connected to DLock. Enter commands (type 'help' or 'quit'):\n");
    char line[DLOCK_MAX_LINE_LEN];

    while (1) {
        printf("dlock> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[len - 1] = '\0';
            len--;
        }

        if (len == 0) continue;

        if (strcasecmp(line, "quit") == 0 || strcasecmp(line, "exit") == 0) {
            break;
        }

        if (strcasecmp(line, "help") == 0) {
            printf("Commands: lock, trylock, unlock, renew, status, create, quit\n");
            continue;
        }

        /* Tokenize input line */
        char *argv[8];
        int argc = 0;
        char *saveptr = NULL;
        char *tok = strtok_r(line, " \t", &saveptr);
        while (tok != NULL && argc < 8) {
            argv[argc++] = tok;
            tok = strtok_r(NULL, " \t", &saveptr);
        }

        if (argc > 0) {
            execute_single_command(conn, argc, argv);
        }
    }
}

int main(int argc, char *argv[]) {
    char host[128] = "127.0.0.1";
    int port = DLOCK_DEFAULT_PORT;
    char client_id[DLOCK_MAX_CLIENT_ID_LEN + 1];
    snprintf(client_id, sizeof(client_id), "client_%d", (int)getpid());

    int opt;
    while ((opt = getopt(argc, argv, "+h:p:i:")) != -1) {
        switch (opt) {
            case 'h':
                strncpy(host, optarg, sizeof(host) - 1);
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'i':
                strncpy(client_id, optarg, sizeof(client_id) - 1);
                break;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    dlock_conn_t *conn = dlock_connect(host, port, client_id);
    if (conn == NULL) {
        fprintf(stderr, "Error: Could not connect to DLock server at %s:%d\n", host, port);
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    if (optind < argc) {
        result = execute_single_command(conn, argc - optind, &argv[optind]);
    } else {
        run_interactive(conn);
    }

    dlock_close(conn);
    return result;
}
