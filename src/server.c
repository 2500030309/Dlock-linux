#include "dlock.h"
#include "protocol.h"

#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/select.h>

#define MAX_WAIT_QUEUE 64

typedef struct {
    char client_id[PROTOCOL_MAX_CLIENT_ID];
    pid_t client_pid;
    char reply_fifo[128];
} wait_node_t;

/* Server Lock Manager State */
static pthread_mutex_t g_lock_mgr_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_lock_is_held = 0;
static char g_owner_id[PROTOCOL_MAX_CLIENT_ID] = "";
static pid_t g_owner_pid = 0;
static int g_total_grants = 0;

static wait_node_t g_wait_queue[MAX_WAIT_QUEUE];
static int g_wait_count = 0;

static volatile sig_atomic_t g_server_running = 1;

const char *request_type_to_string(request_type_t type) {
    switch (type) {
        case REQ_LOCK:     return "LOCK";
        case REQ_UNLOCK:   return "UNLOCK";
        case REQ_STATUS:   return "STATUS";
        case REQ_PING:     return "PING";
        case REQ_SHUTDOWN: return "SHUTDOWN";
        default:           return "UNKNOWN";
    }
}

const char *response_type_to_string(response_type_t type) {
    switch (type) {
        case RESP_GRANTED:     return "GRANTED";
        case RESP_WAITING:     return "WAITING";
        case RESP_RELEASED:    return "RELEASED";
        case RESP_DENIED:      return "DENIED";
        case RESP_STATUS_OK:   return "STATUS_OK";
        case RESP_PONG:        return "PONG";
        case RESP_SHUTDOWN_OK: return "SHUTDOWN_OK";
        case RESP_ERROR:       return "ERROR";
        default:               return "UNKNOWN";
    }
}

static void server_signal_handler(int signo) {
    (void)signo;
    g_server_running = 0;
}

static void send_response(const char *reply_fifo, const dlock_response_t *resp) {
    int wfd = open(reply_fifo, O_WRONLY | O_NONBLOCK);
    if (wfd >= 0) {
        ssize_t ret = write(wfd, resp, sizeof(dlock_response_t));
        (void)ret;
        close(wfd);
    }
}

static void process_request(const dlock_request_t *req) {
    dlock_response_t resp;
    memset(&resp, 0, sizeof(resp));

    pthread_mutex_lock(&g_lock_mgr_mutex);

    snprintf(resp.owner_id, sizeof(resp.owner_id), "%s", g_owner_id);
    resp.owner_pid = g_owner_pid;
    resp.queue_length = g_wait_count;
    resp.total_grants = g_total_grants;

    switch (req->type) {
        case REQ_PING: {
            resp.type = RESP_PONG;
            snprintf(resp.message, sizeof(resp.message), "PONG: DLock Server active (PID: %d)", getpid());
            send_response(req->reply_fifo, &resp);
            dlock_log("PING from client '%s' (PID: %d)", req->client_id, req->client_pid);
            break;
        }

        case REQ_STATUS: {
            resp.type = RESP_STATUS_OK;
            if (g_lock_is_held) {
                snprintf(resp.message, sizeof(resp.message), "Lock HELD by '%s' (PID: %d), Queue: %d, Grants: %d",
                         g_owner_id, g_owner_pid, g_wait_count, g_total_grants);
            } else {
                snprintf(resp.message, sizeof(resp.message), "Lock is FREE. Queue: %d, Lifetime Grants: %d",
                         g_wait_count, g_total_grants);
            }
            send_response(req->reply_fifo, &resp);
            dlock_log("STATUS requested by client '%s'", req->client_id);
            break;
        }

        case REQ_LOCK: {
            if (!g_lock_is_held) {
                g_lock_is_held = 1;
                snprintf(g_owner_id, sizeof(g_owner_id), "%s", req->client_id);
                g_owner_pid = req->client_pid;
                g_total_grants++;

                resp.type = RESP_GRANTED;
                snprintf(resp.owner_id, sizeof(resp.owner_id), "%s", g_owner_id);
                resp.owner_pid = g_owner_pid;
                resp.total_grants = g_total_grants;
                snprintf(resp.message, sizeof(resp.message), "Lock GRANTED to '%s'", req->client_id);

                send_response(req->reply_fifo, &resp);
                printf("%s[GRANT]%s Lock acquired by client '%s' (PID: %d)\n",
                       COLOR_GREEN, COLOR_RESET, req->client_id, req->client_pid);
                dlock_log("Lock GRANTED to client '%s' (PID: %d)", req->client_id, req->client_pid);
            } else if (strcmp(g_owner_id, req->client_id) == 0) {
                resp.type = RESP_GRANTED;
                snprintf(resp.message, sizeof(resp.message), "Client '%s' already owns the lock.", req->client_id);
                send_response(req->reply_fifo, &resp);
            } else {
                if (g_wait_count < MAX_WAIT_QUEUE) {
                    snprintf(g_wait_queue[g_wait_count].client_id,
                             sizeof(g_wait_queue[g_wait_count].client_id), "%s", req->client_id);
                    g_wait_queue[g_wait_count].client_pid = req->client_pid;
                    snprintf(g_wait_queue[g_wait_count].reply_fifo,
                             sizeof(g_wait_queue[g_wait_count].reply_fifo), "%s", req->reply_fifo);
                    g_wait_count++;

                    resp.type = RESP_WAITING;
                    resp.queue_length = g_wait_count;
                    snprintf(resp.message, sizeof(resp.message), "Lock HELD by '%s'. Enqueued at position %d",
                             g_owner_id, g_wait_count);

                    send_response(req->reply_fifo, &resp);
                    printf("%s[WAIT]%s Client '%s' queued (held by '%s', queue pos: %d)\n",
                           COLOR_YELLOW, COLOR_RESET, req->client_id, g_owner_id, g_wait_count);
                    dlock_log("Client '%s' ENQUEUED at position %d", req->client_id, g_wait_count);
                } else {
                    resp.type = RESP_ERROR;
                    snprintf(resp.message, sizeof(resp.message), "Server wait queue capacity full (%d)", MAX_WAIT_QUEUE);
                    send_response(req->reply_fifo, &resp);
                }
            }
            break;
        }

        case REQ_UNLOCK: {
            if (!g_lock_is_held) {
                resp.type = RESP_ERROR;
                snprintf(resp.message, sizeof(resp.message), "Cannot unlock: Lock is not currently held.");
                send_response(req->reply_fifo, &resp);
            } else if (strcmp(g_owner_id, req->client_id) != 0) {
                resp.type = RESP_DENIED;
                snprintf(resp.message, sizeof(resp.message), "Unlock DENIED: '%s' is not owner (held by '%s')",
                         req->client_id, g_owner_id);
                send_response(req->reply_fifo, &resp);
                printf("%s[DENY]%s Client '%s' attempted unauthorized unlock (owner: '%s')\n",
                       COLOR_RED, COLOR_RESET, req->client_id, g_owner_id);
                dlock_log("Unauthorized unlock attempt by '%s'", req->client_id);
            } else {
                resp.type = RESP_RELEASED;
                snprintf(resp.message, sizeof(resp.message), "Lock RELEASED by '%s'", req->client_id);
                send_response(req->reply_fifo, &resp);

                printf("%s[RELEASE]%s Lock released by '%s'\n", COLOR_CYAN, COLOR_RESET, req->client_id);
                dlock_log("Lock RELEASED by client '%s'", req->client_id);

                if (g_wait_count > 0) {
                    wait_node_t next_client = g_wait_queue[0];
                    for (int i = 0; i < g_wait_count - 1; i++) {
                        g_wait_queue[i] = g_wait_queue[i + 1];
                    }
                    g_wait_count--;

                    snprintf(g_owner_id, sizeof(g_owner_id), "%s", next_client.client_id);
                    g_owner_pid = next_client.client_pid;
                    g_total_grants++;

                    dlock_response_t grant_resp;
                    memset(&grant_resp, 0, sizeof(grant_resp));
                    grant_resp.type = RESP_GRANTED;
                    snprintf(grant_resp.owner_id, sizeof(grant_resp.owner_id), "%s", g_owner_id);
                    grant_resp.owner_pid = g_owner_pid;
                    grant_resp.queue_length = g_wait_count;
                    grant_resp.total_grants = g_total_grants;
                    snprintf(grant_resp.message, sizeof(grant_resp.message),
                             "Lock GRANTED to queued client '%s'", next_client.client_id);

                    send_response(next_client.reply_fifo, &grant_resp);
                    printf("%s[HANDOVER]%s Lock handed over to waiting client '%s' (PID: %d)\n",
                           COLOR_GREEN, COLOR_RESET, next_client.client_id, next_client.client_pid);
                    dlock_log("Lock HANDOVER to '%s' (PID: %d)", next_client.client_id, next_client.client_pid);
                } else {
                    g_lock_is_held = 0;
                    memset(g_owner_id, 0, sizeof(g_owner_id));
                    g_owner_pid = 0;
                }
            }
            break;
        }

        case REQ_SHUTDOWN: {
            resp.type = RESP_SHUTDOWN_OK;
            snprintf(resp.message, sizeof(resp.message), "DLock Server initiating graceful shutdown.");
            send_response(req->reply_fifo, &resp);
            printf("%s[SHUTDOWN]%s Shutdown command received from '%s'.\n",
                   COLOR_YELLOW, COLOR_RESET, req->client_id);
            dlock_log("Server SHUTDOWN requested by '%s'", req->client_id);
            g_server_running = 0;
            break;
        }
    }

    pthread_mutex_unlock(&g_lock_mgr_mutex);
}

int run_server(void) {
    print_banner();
    const char *fifo_path = resolve_fifo_path();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = server_signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    unlink(fifo_path);
    if (mkfifo(fifo_path, 0666) < 0) {
        perror("Failed to create server FIFO");
        return 1;
    }

    /* Open server FIFO in O_RDWR mode so reader never receives EOF on client disconnect */
    int sfd = open(fifo_path, O_RDWR);
    if (sfd < 0) {
        perror("Failed to open server FIFO");
        unlink(fifo_path);
        return 1;
    }

    printf("%s[SERVER ONLINE]%s Listening for lock requests on: %s\n",
           COLOR_GREEN, COLOR_RESET, fifo_path);
    printf("  Log path: %s\n", DEFAULT_LOG_PATH);
    printf("  Send SIGINT (Ctrl+C) or './bin/dlock client shutdown <ID>' to exit.\n\n");
    dlock_log("DLock Server started (PID: %d, FIFO: %s)", getpid(), fifo_path);

    while (g_server_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sfd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int sel = select(sfd + 1, &read_fds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(sfd, &read_fds)) {
            dlock_request_t req;
            ssize_t bytes = read(sfd, &req, sizeof(dlock_request_t));
            if (bytes == sizeof(dlock_request_t)) {
                process_request(&req);
            }
        }
    }

    close(sfd);
    printf("\n%s[SERVER SHUTDOWN]%s Releasing resources and unlinking FIFO...\n",
           COLOR_YELLOW, COLOR_RESET);
    unlink(fifo_path);
    dlock_log("DLock Server terminated cleanly");
    printf("Server shutdown complete.\n");
    return 0;
}
