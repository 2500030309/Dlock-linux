#include "dlock.h"
#include "protocol.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

/* Global state for client session */
static int g_holds_lock = 0;

static int send_request_and_wait(request_type_t req_type, const char *client_id, dlock_response_t *resp_out) {
    const char *server_fifo = resolve_fifo_path();

    char reply_fifo[128];
    snprintf(reply_fifo, sizeof(reply_fifo), "/tmp/dlock_reply_%u.fifo", (unsigned int)getpid());
    unlink(reply_fifo);

    if (mkfifo(reply_fifo, 0666) < 0) {
        perror("Failed to create client reply FIFO");
        return -1;
    }

    int rfd = open(reply_fifo, O_RDWR);
    if (rfd < 0) {
        perror("Failed to open reply FIFO");
        unlink(reply_fifo);
        return -1;
    }

    dlock_request_t req;
    memset(&req, 0, sizeof(req));
    req.type = req_type;
    snprintf(req.client_id, sizeof(req.client_id), "%s", client_id);
    req.client_pid = getpid();
    snprintf(req.reply_fifo, sizeof(req.reply_fifo), "%s", reply_fifo);

    int sfd = open(server_fifo, O_WRONLY | O_NONBLOCK);
    if (sfd < 0) {
        fprintf(stderr, "%sError: DLock server is not running on '%s'%s\n",
                COLOR_RED, server_fifo, COLOR_RESET);
        fprintf(stderr, "Start the server first in another terminal: %s./bin/dlock server%s\n",
                COLOR_CYAN, COLOR_RESET);
        close(rfd);
        unlink(reply_fifo);
        return -1;
    }

    ssize_t written = write(sfd, &req, sizeof(dlock_request_t));
    close(sfd);

    if (written != sizeof(dlock_request_t)) {
        fprintf(stderr, "%sFailed to send request packet%s\n", COLOR_RED, COLOR_RESET);
        close(rfd);
        unlink(reply_fifo);
        return -1;
    }

    memset(resp_out, 0, sizeof(dlock_response_t));
    ssize_t bytes_read = read(rfd, resp_out, sizeof(dlock_response_t));

    if (bytes_read != sizeof(dlock_response_t)) {
        fprintf(stderr, "%sFailed to receive response packet%s\n", COLOR_RED, COLOR_RESET);
        close(rfd);
        unlink(reply_fifo);
        return -1;
    }

    /* If waiting, block until grant arrives */
    if (resp_out->type == RESP_WAITING && req_type == REQ_LOCK) {
        printf("%s[QUEUE]%s Lock is held by '%s'. Waiting in queue for grant...\n",
               COLOR_YELLOW, COLOR_RESET, resp_out->owner_id);
        fflush(stdout);

        memset(resp_out, 0, sizeof(dlock_response_t));
        ssize_t grant_bytes = read(rfd, resp_out, sizeof(dlock_response_t));
        (void)grant_bytes;
    }

    close(rfd);
    unlink(reply_fifo);
    return 0;
}

static void access_shared_resource_write(const char *client_id, const char *data) {
    if (!g_holds_lock) {
        printf("%s[WARNING]%s Client '%s' does NOT currently hold the lock!\n",
               COLOR_RED, COLOR_RESET, client_id);
        printf("Writing without mutual exclusion violates critical section integrity.\n");
    }

    const char *res_path = resolve_resource_path();
    int fd = open(res_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) {
        perror("Failed to open shared resource");
        return;
    }

    char record[512];
    time_t now = time(NULL);
    snprintf(record, sizeof(record), "[%ld] [%s (PID:%d)]: %s\n", (long)now, client_id, getpid(), data);
    ssize_t w = write(fd, record, strlen(record));
    (void)w;
    close(fd);

    printf("%s[RESOURCE WRITE]%s Successfully appended to '%s':\n", COLOR_GREEN, COLOR_RESET, res_path);
    printf("  %s\n", record);
}

static void access_shared_resource_read(void) {
    const char *res_path = resolve_resource_path();
    int fd = open(res_path, O_RDONLY);
    if (fd < 0) {
        printf("  Shared resource file is currently empty or not created yet.\n");
        return;
    }

    printf("%s[RESOURCE READ]%s Contents of '%s':\n", COLOR_CYAN, COLOR_RESET, res_path);
    char buf[1024];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[r] = '\0';
        printf("%s", buf);
    }
    close(fd);
    printf("\n");
}

/* Interactive REPL Shell */
static int run_interactive_shell(const char *client_id) {
    print_banner();
    printf("%sInteractive DLock Client Shell%s | Client ID: %s%s%s (PID: %d)\n",
           COLOR_GREEN, COLOR_RESET, COLOR_BOLD, client_id, COLOR_RESET, getpid());
    printf("Type 'help' for commands, 'exit' to quit.\n\n");

    char line[512];
    while (1) {
        printf("%s[dlock:%s]> %s", COLOR_YELLOW, client_id, COLOR_RESET);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (strlen(line) == 0) continue;

        char cmd[64] = "";
        char arg[448] = "";
        sscanf(line, "%63s %[^\n]", cmd, arg);

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            if (g_holds_lock) {
                printf("Auto-releasing lock before exit...\n");
                dlock_response_t resp;
                send_request_and_wait(REQ_UNLOCK, client_id, &resp);
            }
            printf("Exiting DLock shell.\n");
            break;
        } else if (strcmp(cmd, "help") == 0) {
            printf("  Available shell commands:\n");
            printf("    lock          - Acquire exclusive lock (blocks if held)\n");
            printf("    unlock        - Release currently held lock\n");
            printf("    status        - Query server lock owner and wait queue\n");
            printf("    write <text>  - Write message to protected shared resource\n");
            printf("    read          - Read protected shared resource\n");
            printf("    ping          - Ping server health\n");
            printf("    exit / quit   - Release lock and quit\n");
        } else if (strcmp(cmd, "lock") == 0) {
            dlock_response_t resp;
            if (send_request_and_wait(REQ_LOCK, client_id, &resp) == 0) {
                if (resp.type == RESP_GRANTED) {
                    g_holds_lock = 1;
                    printf("%s[GRANTED]%s %s (Owner PID: %d, Lifetime Grants: %d)\n",
                           COLOR_GREEN, COLOR_RESET, resp.message, resp.owner_pid, resp.total_grants);
                } else {
                    printf("%s[%s]%s %s\n", COLOR_RED, response_type_to_string(resp.type), COLOR_RESET, resp.message);
                }
            }
        } else if (strcmp(cmd, "unlock") == 0) {
            dlock_response_t resp;
            if (send_request_and_wait(REQ_UNLOCK, client_id, &resp) == 0) {
                if (resp.type == RESP_RELEASED) {
                    g_holds_lock = 0;
                    printf("%s[RELEASED]%s %s\n", COLOR_GREEN, COLOR_RESET, resp.message);
                } else {
                    printf("%s[%s]%s %s\n", COLOR_RED, response_type_to_string(resp.type), COLOR_RESET, resp.message);
                }
            }
        } else if (strcmp(cmd, "status") == 0) {
            dlock_response_t resp;
            if (send_request_and_wait(REQ_STATUS, client_id, &resp) == 0) {
                printf("%s[STATUS]%s %s\n", COLOR_CYAN, COLOR_RESET, resp.message);
            }
        } else if (strcmp(cmd, "ping") == 0) {
            dlock_response_t resp;
            if (send_request_and_wait(REQ_PING, client_id, &resp) == 0) {
                printf("%s[PING]%s %s\n", COLOR_CYAN, COLOR_RESET, resp.message);
            }
        } else if (strcmp(cmd, "write") == 0) {
            if (strlen(arg) == 0) {
                printf("Usage: write <text to write into shared resource>\n");
            } else {
                access_shared_resource_write(client_id, arg);
            }
        } else if (strcmp(cmd, "read") == 0) {
            access_shared_resource_read();
        } else {
            printf("%sUnknown shell command: '%s'. Type 'help' for available commands.%s\n",
                   COLOR_RED, cmd, COLOR_RESET);
        }
    }
    return 0;
}

int run_client(int argc, char *argv[]) {
    if (argc < 1) {
        fprintf(stderr, "%sUsage: dlock client <lock|unlock|status|ping|write|read|shell|shutdown> <CLIENT_ID> [args]%s\n",
                COLOR_YELLOW, COLOR_RESET);
        return 1;
    }

    const char *action = argv[0];
    const char *client_id = (argc >= 2) ? argv[1] : "CLIENT_DEFAULT";

    if (strcmp(action, "shell") == 0) {
        return run_interactive_shell(client_id);
    }

    if (strcmp(action, "write") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: dlock client write <CLIENT_ID> <text>\n");
            return 1;
        }
        access_shared_resource_write(client_id, argv[2]);
        return 0;
    }

    if (strcmp(action, "read") == 0) {
        access_shared_resource_read();
        return 0;
    }

    request_type_t req_type;
    if (strcmp(action, "lock") == 0) req_type = REQ_LOCK;
    else if (strcmp(action, "unlock") == 0) req_type = REQ_UNLOCK;
    else if (strcmp(action, "status") == 0) req_type = REQ_STATUS;
    else if (strcmp(action, "ping") == 0) req_type = REQ_PING;
    else if (strcmp(action, "shutdown") == 0) req_type = REQ_SHUTDOWN;
    else {
        fprintf(stderr, "%sUnknown client action: %s%s\n", COLOR_RED, action, COLOR_RESET);
        return 1;
    }

    dlock_response_t resp;
    if (send_request_and_wait(req_type, client_id, &resp) != 0) {
        return 1;
    }

    if (resp.type == RESP_GRANTED) {
        g_holds_lock = 1;
        printf("%s[SUCCESS: GRANTED]%s %s (Owner PID: %d, Lifetime Grants: %d)\n",
               COLOR_GREEN, COLOR_RESET, resp.message, resp.owner_pid, resp.total_grants);
    } else if (resp.type == RESP_RELEASED) {
        g_holds_lock = 0;
        printf("%s[SUCCESS: RELEASED]%s %s\n", COLOR_GREEN, COLOR_RESET, resp.message);
    } else if (resp.type == RESP_STATUS_OK || resp.type == RESP_PONG || resp.type == RESP_SHUTDOWN_OK) {
        printf("%s[%s]%s %s\n", COLOR_CYAN, response_type_to_string(resp.type), COLOR_RESET, resp.message);
    } else {
        printf("%s[%s]%s %s\n", COLOR_RED, response_type_to_string(resp.type), COLOR_RESET, resp.message);
    }

    return (resp.type == RESP_ERROR || resp.type == RESP_DENIED) ? 1 : 0;
}
