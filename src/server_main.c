/**
 * @file server_main.c
 * @brief Main entry point for the DLock server daemon (bin/dlockd).
 *
 * Parses command-line arguments, populates configuration parameters,
 * and starts the server listen loop.
 */

#include "server.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -p <port>         TCP port to listen on (default: %d)\n", DLOCK_DEFAULT_PORT);
    printf("  -t <workers>      Number of worker threads (default: %d)\n", DLOCK_DEFAULT_WORKERS);
    printf("  -m <max_clients>  Max concurrent connected clients (default: %d)\n", DLOCK_DEFAULT_MAX_CLIENTS);
    printf("  -w <wal_path>     Write-ahead log file path (default: %s)\n", DLOCK_DEFAULT_WAL_PATH);
    printf("  -a <admin_fifo>   Admin FIFO control path (default: %s)\n", DLOCK_ADMIN_FIFO_PATH);
    printf("  -l <log_file>     Log file path (default: %s)\n", DLOCK_DEFAULT_LOG_PATH);
    printf("  -v <log_level>    Log verbosity (DEBUG, INFO, WARN, ERROR) (default: INFO)\n");
    printf("  -h                Display this help message\n");
}

int main(int argc, char *argv[]) {
    server_config_t config;
    config.port = DLOCK_DEFAULT_PORT;
    config.num_workers = DLOCK_DEFAULT_WORKERS;
    config.max_clients = DLOCK_DEFAULT_MAX_CLIENTS;
    config.wal_path = DLOCK_DEFAULT_WAL_PATH;
    config.admin_path = DLOCK_ADMIN_FIFO_PATH;
    config.log_path = DLOCK_DEFAULT_LOG_PATH;
    config.log_level = LOG_LEVEL_INFO;

    int opt;
    while ((opt = getopt(argc, argv, "p:t:m:w:a:l:v:h")) != -1) {
        switch (opt) {
            case 'p':
                config.port = atoi(optarg);
                break;
            case 't':
                config.num_workers = atoi(optarg);
                break;
            case 'm':
                config.max_clients = atoi(optarg);
                break;
            case 'w':
                config.wal_path = optarg;
                break;
            case 'a':
                config.admin_path = optarg;
                break;
            case 'l':
                config.log_path = optarg;
                break;
            case 'v':
                config.log_level = logger_parse_level(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (config.port <= 0 || config.port > 65535) {
        fprintf(stderr, "Error: Invalid port number %d\n", config.port);
        return EXIT_FAILURE;
    }

    return server_run(&config);
}
