/**
 * @file server.c
 * @brief TCP network listen/accept loop and client session handling.
 *
 * Implements the network server architecture:
 * - Listens on TCP port with SO_REUSEADDR.
 * - Multiplexes listening socket and self-pipe using poll().
 * - Enforces client concurrency limit using POSIX counting semaphore (sem_t).
 * - Delegates accepted connections to a bounded thread pool.
 * - Handles client command parsing, HELLO handshake, and disconnect cleanup.
 */

#include "server.h"
#include "protocol.h"
#include "lock_table.h"
#include "thread_pool.h"
#include "lease.h"
#include "wal.h"
#include "signals.h"
#include "admin.h"
#include "stats.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdatomic.h>

static atomic_bool s_server_running = true;
static sem_t s_client_semaphore;
static int s_listen_fd = -1;

void server_stop(void) {
    atomic_store(&s_server_running, false);
    signals_trigger_shutdown();
}

/* Helper to write full buffer to socket, handling partial writes and EINTR */
static bool send_all(int fd, const char *buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = write(fd, buf + total_sent, len - total_sent);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        total_sent += (size_t)sent;
    }
    return true;
}

void server_handle_client_session(int client_fd) {
    stats_inc_client_connect();

    char client_id[DLOCK_MAX_CLIENT_ID_LEN + 1] = {0};
    bool hello_received = false;

    FILE *fp = fdopen(client_fd, "r");
    if (fp == NULL) {
        close(client_fd);
        sem_post(&s_client_semaphore);
        stats_dec_client_disconnect();
        return;
    }

    char line[DLOCK_MAX_LINE_LEN + 2];
    while (atomic_load(&s_server_running) && fgets(line, sizeof(line), fp) != NULL) {
        request_t req;
        char err_msg[128] = {0};
        int parse_res = protocol_parse_request(line, &req, err_msg, sizeof(err_msg));

        if (parse_res != DLOCK_SUCCESS) {
            response_t resp;
            resp.type = RESP_ERR_BADCMD;
            strncpy(resp.message, err_msg, sizeof(resp.message) - 1);
            char resp_wire[DLOCK_MAX_LINE_LEN];
            protocol_format_response(&resp, resp_wire, sizeof(resp_wire));
            send_all(client_fd, resp_wire, strlen(resp_wire));
            continue;
        }

        /* Enforce HELLO must be first command */
        if (!hello_received) {
            if (req.type != CMD_HELLO) {
                response_t resp = {.type = RESP_ERR_NOHELLO};
                char resp_wire[DLOCK_MAX_LINE_LEN];
                protocol_format_response(&resp, resp_wire, sizeof(resp_wire));
                send_all(client_fd, resp_wire, strlen(resp_wire));
                continue;
            }
            hello_received = true;
            strncpy(client_id, req.client_id, sizeof(client_id) - 1);
            response_t resp = {.type = RESP_OK};
            char resp_wire[DLOCK_MAX_LINE_LEN];
            protocol_format_response(&resp, resp_wire, sizeof(resp_wire));
            send_all(client_fd, resp_wire, strlen(resp_wire));
            logger_log(LOG_LEVEL_INFO, "Client '%s' registered session (fd %d)", client_id, client_fd);
            continue;
        }

        /* Handle subsequent commands */
        if (req.type == CMD_QUIT) {
            logger_log(LOG_LEVEL_INFO, "Client '%s' sent QUIT", client_id);
            break;
        }

        if (req.type == CMD_LOCK) {
            uint64_t token = 0;
            int acq = lock_table_acquire(client_id, req.lock_name, req.timeout_ms, req.lease_ms, &token);
            response_t resp;
            if (acq == DLOCK_SUCCESS) {
                resp.type = RESP_OK_TOKEN;
                resp.token = token;
                lease_notify();
            } else if (acq == DLOCK_ERR_TIMEOUT) {
                resp.type = RESP_ERR_TIMEOUT;
            } else if (acq == DLOCK_ERR_DEADLOCK) {
                resp.type = RESP_ERR_DEADLOCK;
            } else if (acq == DLOCK_ERR_BUSY) {
                resp.type = RESP_ERR_BUSY;
            } else {
                resp.type = RESP_ERR_BADCMD;
                snprintf(resp.message, sizeof(resp.message), "Lock acquisition failed");
            }
            char resp_wire[DLOCK_MAX_LINE_LEN];
            protocol_format_response(&resp, resp_wire, sizeof(resp_wire));
            send_all(client_fd, resp_wire, strlen(resp_wire));
            continue;
        }

        if (req.type == CMD_TRYLOCK) {
            uint64_t token = 0;
            int acq = lock_table_trylock(client_id, req.lock_name, req.lease_ms, &token);
            response_t resp;
            if (acq == DLOCK_SUCCESS) {
                resp.type = RESP_OK_TOKEN;
                resp.token = token;
                lease_notify();
            } else {
                resp.type = RESP_ERR_BUSY;
            }
            char resp_wire[DLOCK_MAX_LINE_LEN];
            protocol_format_response(&resp, resp_wire, sizeof(resp_wire));
            send_all(client_fd, resp_wire, strlen(resp_wire));
            continue;
        }

        if (req.type == CMD_UNLOCK) {
            int unl = lock_table_unlock(client_id, req.lock_name, req.token);
            response_t resp;
            if (unl == DLOCK_SUCCESS) {
                resp.type = RESP_OK;
            } else if (unl == DLOCK_ERR_EXPIRED) {
                resp.type = RESP_ERR_EXPIRED;
            } else {
                resp.type = RESP_ERR_NOTOWNER;
            }
            char resp_wire[DLOCK_MAX_LINE_LEN];
            protocol_format_response(&resp, resp_wire, sizeof(resp_wire));
            send_all(client_fd, resp_wire, strlen(resp_wire));
            continue;
        }

        if (req.type == CMD_RENEW) {
            int ren = lock_table_renew(client_id, req.lock_name, req.token, req.lease_ms);
            response_t resp;
            if (ren == DLOCK_SUCCESS) {
                resp.type = RESP_OK;
                lease_notify();
            } else if (ren == DLOCK_ERR_EXPIRED) {
                resp.type = RESP_ERR_EXPIRED;
            } else {
                resp.type = RESP_ERR_NOTOWNER;
            }
            char resp_wire[DLOCK_MAX_LINE_LEN];
            protocol_format_response(&resp, resp_wire, sizeof(resp_wire));
            send_all(client_fd, resp_wire, strlen(resp_wire));
            continue;
        }

        if (req.type == CMD_CREATE) {
            int cr = lock_table_create(req.lock_name, req.capacity);
            response_t resp;
            if (cr == DLOCK_SUCCESS) {
                resp.type = RESP_OK;
            } else {
                resp.type = RESP_ERR_BADCMD;
                snprintf(resp.message, sizeof(resp.message), "Creation failed");
            }
            char resp_wire[DLOCK_MAX_LINE_LEN];
            protocol_format_response(&resp, resp_wire, sizeof(resp_wire));
            send_all(client_fd, resp_wire, strlen(resp_wire));
            continue;
        }

        if (req.type == CMD_STATUS) {
            char status_buf[4096] = {0};
            lock_table_get_status(req.lock_name, status_buf, sizeof(status_buf));

            /* Stream line by line with INFO prefix */
            char *line_ptr = strtok(status_buf, "\n");
            while (line_ptr != NULL) {
                response_t info_resp;
                info_resp.type = RESP_INFO;
                strncpy(info_resp.message, line_ptr, sizeof(info_resp.message) - 1);
                char resp_wire[DLOCK_MAX_LINE_LEN];
                protocol_format_response(&info_resp, resp_wire, sizeof(resp_wire));
                send_all(client_fd, resp_wire, strlen(resp_wire));
                line_ptr = strtok(NULL, "\n");
            }

            response_t end_resp = {.type = RESP_END};
            char resp_wire[DLOCK_MAX_LINE_LEN];
            protocol_format_response(&end_resp, resp_wire, sizeof(resp_wire));
            send_all(client_fd, resp_wire, strlen(resp_wire));
            continue;
        }
    }

    /* Client disconnected or closed session: release all locks held by this client */
    if (hello_received && client_id[0] != '\0') {
        int released = lock_table_release_all_for_client(client_id);
        logger_log(LOG_LEVEL_INFO, "Client '%s' disconnected; released/dequeued %d lock entries",
                   client_id, released);
    }

    fclose(fp);
    sem_post(&s_client_semaphore);
    stats_dec_client_disconnect();
}

int server_run(const server_config_t *config) {
    if (config == NULL) {
        return -1;
    }

    /* 1. Initialize Logger */
    logger_init(config->log_path, config->log_level);
    logger_log(LOG_LEVEL_INFO, "=== Starting DLock Distributed Mutex Server ===");

    /* 2. Initialize Signals & Self-Pipe */
    if (signals_init() < 0) {
        logger_log(LOG_LEVEL_ERROR, "Failed to initialize signal handlers");
        return -1;
    }

    /* 3. Initialize Write-Ahead Log (WAL) and Recover Highest Fencing Token */
    uint64_t recovered_token = 0;
    wal_recover(config->wal_path, &recovered_token);
    if (wal_init(config->wal_path) < 0) {
        logger_log(LOG_LEVEL_ERROR, "Failed to initialize WAL at %s", config->wal_path);
        return -1;
    }

    /* 4. Initialize Lock Table & Lease Subsystem */
    lock_table_init();
    lock_table_set_fencing_token(recovered_token);

    if (lease_init() < 0 || lease_start() < 0) {
        logger_log(LOG_LEVEL_ERROR, "Failed to start lease management thread");
        return -1;
    }

    /* 5. Initialize Admin FIFO channel */
    if (admin_init(config->admin_path) == 0) {
        admin_start();
    }

    /* 6. Initialize Counting Semaphore for Client Concurrency */
    int max_clients = (config->max_clients > 0) ? config->max_clients : DLOCK_DEFAULT_MAX_CLIENTS;
    sem_init(&s_client_semaphore, 0, (unsigned int)max_clients);

    /* 7. Initialize Worker Thread Pool */
    int workers = (config->num_workers > 0) ? config->num_workers : DLOCK_DEFAULT_WORKERS;
    thread_pool_t *pool = thread_pool_create(workers, DLOCK_JOB_QUEUE_CAPACITY, server_handle_client_session);
    if (pool == NULL) {
        logger_log(LOG_LEVEL_ERROR, "Failed to create thread pool");
        return -1;
    }

    /* 8. Setup TCP Listen Socket */
    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        logger_log(LOG_LEVEL_ERROR, "Socket creation failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons((uint16_t)config->port);

    if (bind(s_listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        logger_log(LOG_LEVEL_ERROR, "Bind failed on port %d: %s", config->port, strerror(errno));
        close(s_listen_fd);
        return -1;
    }

    if (listen(s_listen_fd, 128) < 0) {
        logger_log(LOG_LEVEL_ERROR, "Listen failed: %s", strerror(errno));
        close(s_listen_fd);
        return -1;
    }

    logger_log(LOG_LEVEL_INFO, "DLock server listening on 0.0.0.0:%d (workers=%d, max_clients=%d)",
               config->port, workers, max_clients);

    /* 9. Main poll() accept loop */
    int sig_read_fd = signals_get_pipe_read_fd();
    struct pollfd pfd[2];
    pfd[0].fd = s_listen_fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = sig_read_fd;
    pfd[1].events = POLLIN;

    atomic_store(&s_server_running, true);

    while (atomic_load(&s_server_running)) {
        int poll_ret = poll(pfd, 2, -1);
        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            logger_log(LOG_LEVEL_ERROR, "poll() returned error: %s", strerror(errno));
            break;
        }

        /* Check signal self-pipe */
        if (pfd[1].revents & POLLIN) {
            char sig_byte = 0;
            while (read(sig_read_fd, &sig_byte, 1) > 0) {
                if (sig_byte == 'S') {
                    logger_log(LOG_LEVEL_INFO, "Termination signal received; initiating shutdown...");
                    atomic_store(&s_server_running, false);
                } else if (sig_byte == 'D') {
                    logger_log(LOG_LEVEL_INFO, "Diagnostic dump triggered by SIGUSR1");
                    char dump_buf[4096];
                    lock_table_get_status(NULL, dump_buf, sizeof(dump_buf));
                    printf("=== SIGUSR1 Lock Table Dump ===\n%s================================\n", dump_buf);
                    stats_dump(stdout);
                }
            }
            if (!atomic_load(&s_server_running)) {
                break;
            }
        }

        /* Check new incoming connection */
        if (pfd[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(s_listen_fd, (struct sockaddr *)&client_addr, &client_len);

            if (client_fd < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                logger_log(LOG_LEVEL_WARN, "accept() error: %s", strerror(errno));
                continue;
            }

            /* Check client capacity limit via semaphore */
            if (sem_trywait(&s_client_semaphore) != 0) {
                logger_log(LOG_LEVEL_WARN, "Max concurrent client limit (%d) reached; rejecting fd %d",
                           max_clients, client_fd);
                const char *busy_msg = "ERR BUSY\n";
                (void)write(client_fd, busy_msg, strlen(busy_msg));
                close(client_fd);
                continue;
            }

            /* Enqueue accepted connection into worker thread pool */
            if (thread_pool_enqueue(pool, client_fd) != 0) {
                sem_post(&s_client_semaphore);
                close(client_fd);
            }
        }
    }

    /* 10. Graceful Shutdown & Teardown */
    logger_log(LOG_LEVEL_INFO, "Shutting down DLock server...");
    close(s_listen_fd);

    thread_pool_stop(pool);
    thread_pool_destroy(pool);

    lease_stop();
    lease_cleanup();

    admin_stop();
    admin_cleanup();

    lock_table_cleanup();
    wal_close();
    signals_cleanup();
    sem_destroy(&s_client_semaphore);

    logger_log(LOG_LEVEL_INFO, "DLock shutdown complete. Clean exit.");
    logger_cleanup();

    return 0;
}
