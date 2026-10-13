/**
 * @file server.h
 * @brief Network listening, accept loop, and client session handler.
 *
 * Implements the core network server loop:
 * - Creates TCP socket, sets SO_REUSEADDR, binds, and listens.
 * - Uses poll() to multiplex between client incoming connections and
 *   the shutdown self-pipe.
 * - Enforces client concurrency limits via a counting semaphore.
 * - Dispatches accepted connections into the worker thread pool.
 */

#ifndef DLOCK_SERVER_H
#define DLOCK_SERVER_H

#include "common.h"
#include "logger.h"

typedef struct {
    int port;
    int num_workers;
    int max_clients;
    const char *wal_path;
    const char *admin_path;
    const char *log_path;
    log_level_t log_level;
} server_config_t;

/**
 * @brief Initialize all subsystems, start background threads, and run the accept loop.
 *
 * Runs until a termination signal or administrative shutdown is received.
 *
 * @param config Pointer to server configuration settings.
 * @return int 0 on clean exit, non-zero on startup failure.
 */
int server_run(const server_config_t *config);

/**
 * @brief Worker thread entry point for handling an individual client connection.
 *
 * Reads line-by-line requests, enforces HELLO handshake, invokes lock table operations,
 * and releases held locks upon disconnect.
 *
 * @param client_fd Socket descriptor for the client connection.
 */
void server_handle_client_session(int client_fd);

/**
 * @brief Signal the server accept loop to stop.
 */
void server_stop(void);

#endif /* DLOCK_SERVER_H */
