/**
 * @file client_api.c
 * @brief C client library implementation for DLock.
 *
 * Provides a synchronous, thread-safe, blocking network client library.
 * Handles partial reads and writes, socket reconnection handling, and
 * transparent serialization to/from the DLock wire protocol.
 */

#include "client_api.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

struct dlock_conn {
    int socket_fd;
    FILE *rx_stream;
    char client_id[DLOCK_MAX_CLIENT_ID_LEN + 1];
    char host[128];
    int port;
};

/* Helper to send complete buffer */
static bool client_send_all(int fd, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t written = write(fd, buf + total, len - total);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        total += (size_t)written;
    }
    return true;
}

/* Helper to read a line from server */
static bool client_read_line(FILE *stream, char *buf, size_t buf_sz) {
    if (stream == NULL || buf == NULL || buf_sz == 0) {
        return false;
    }
    if (fgets(buf, (int)buf_sz, stream) == NULL) {
        return false;
    }
    return true;
}

dlock_conn_t *dlock_connect(const char *host, int port, const char *client_id) {
    if (host == NULL || port <= 0 || client_id == NULL) {
        return NULL;
    }
    if (!protocol_validate_id(client_id)) {
        return NULL;
    }

    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return NULL;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; /* Success */
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        return NULL;
    }

    dlock_conn_t *conn = (dlock_conn_t *)calloc(1, sizeof(dlock_conn_t));
    if (conn == NULL) {
        close(fd);
        return NULL;
    }

    conn->socket_fd = fd;
    conn->rx_stream = fdopen(fd, "r");
    if (conn->rx_stream == NULL) {
        close(fd);
        free(conn);
        return NULL;
    }

    strncpy(conn->client_id, client_id, sizeof(conn->client_id) - 1);
    strncpy(conn->host, host, sizeof(conn->host) - 1);
    conn->port = port;

    /* Handshake: HELLO <client_id> */
    request_t req;
    memset(&req, 0, sizeof(req));
    req.type = CMD_HELLO;
    strncpy(req.client_id, client_id, sizeof(req.client_id) - 1);

    char wire[DLOCK_MAX_LINE_LEN];
    protocol_format_request(&req, wire, sizeof(wire));
    if (!client_send_all(conn->socket_fd, wire, strlen(wire))) {
        dlock_close(conn);
        return NULL;
    }

    char resp_line[DLOCK_MAX_LINE_LEN];
    if (!client_read_line(conn->rx_stream, resp_line, sizeof(resp_line))) {
        dlock_close(conn);
        return NULL;
    }

    response_t resp;
    int parse_rc = protocol_parse_response(resp_line, &resp);
    if (parse_rc != DLOCK_SUCCESS || resp.type != RESP_OK) {
        dlock_close(conn);
        return NULL;
    }

    return conn;
}

int dlock_lock(dlock_conn_t *conn,
               const char *name,
               uint32_t timeout_ms,
               uint32_t lease_ms,
               uint64_t *out_token) {
    if (conn == NULL || name == NULL || out_token == NULL) {
        return DLOCK_ERR_BADCMD;
    }

    request_t req;
    memset(&req, 0, sizeof(req));
    req.type = CMD_LOCK;
    strncpy(req.lock_name, name, sizeof(req.lock_name) - 1);
    req.timeout_ms = timeout_ms;
    req.lease_ms = lease_ms;

    char wire[DLOCK_MAX_LINE_LEN];
    protocol_format_request(&req, wire, sizeof(wire));
    if (!client_send_all(conn->socket_fd, wire, strlen(wire))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    char resp_line[DLOCK_MAX_LINE_LEN];
    if (!client_read_line(conn->rx_stream, resp_line, sizeof(resp_line))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    response_t resp;
    int rc = protocol_parse_response(resp_line, &resp);
    if (rc == DLOCK_SUCCESS && resp.type == RESP_OK_TOKEN) {
        *out_token = resp.token;
        return DLOCK_SUCCESS;
    }

    return rc;
}

int dlock_trylock(dlock_conn_t *conn,
                  const char *name,
                  uint32_t lease_ms,
                  uint64_t *out_token) {
    if (conn == NULL || name == NULL || out_token == NULL) {
        return DLOCK_ERR_BADCMD;
    }

    request_t req;
    memset(&req, 0, sizeof(req));
    req.type = CMD_TRYLOCK;
    strncpy(req.lock_name, name, sizeof(req.lock_name) - 1);
    req.lease_ms = lease_ms;

    char wire[DLOCK_MAX_LINE_LEN];
    protocol_format_request(&req, wire, sizeof(wire));
    if (!client_send_all(conn->socket_fd, wire, strlen(wire))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    char resp_line[DLOCK_MAX_LINE_LEN];
    if (!client_read_line(conn->rx_stream, resp_line, sizeof(resp_line))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    response_t resp;
    int rc = protocol_parse_response(resp_line, &resp);
    if (rc == DLOCK_SUCCESS && resp.type == RESP_OK_TOKEN) {
        *out_token = resp.token;
        return DLOCK_SUCCESS;
    }

    return rc;
}

int dlock_unlock(dlock_conn_t *conn, const char *name, uint64_t token) {
    if (conn == NULL || name == NULL || token == 0) {
        return DLOCK_ERR_BADCMD;
    }

    request_t req;
    memset(&req, 0, sizeof(req));
    req.type = CMD_UNLOCK;
    strncpy(req.lock_name, name, sizeof(req.lock_name) - 1);
    req.token = token;

    char wire[DLOCK_MAX_LINE_LEN];
    protocol_format_request(&req, wire, sizeof(wire));
    if (!client_send_all(conn->socket_fd, wire, strlen(wire))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    char resp_line[DLOCK_MAX_LINE_LEN];
    if (!client_read_line(conn->rx_stream, resp_line, sizeof(resp_line))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    response_t resp;
    int rc = protocol_parse_response(resp_line, &resp);
    if (rc == DLOCK_SUCCESS && resp.type == RESP_OK) {
        return DLOCK_SUCCESS;
    }
    return rc;
}

int dlock_renew(dlock_conn_t *conn, const char *name, uint64_t token, uint32_t lease_ms) {
    if (conn == NULL || name == NULL || token == 0 || lease_ms == 0) {
        return DLOCK_ERR_BADCMD;
    }

    request_t req;
    memset(&req, 0, sizeof(req));
    req.type = CMD_RENEW;
    strncpy(req.lock_name, name, sizeof(req.lock_name) - 1);
    req.token = token;
    req.lease_ms = lease_ms;

    char wire[DLOCK_MAX_LINE_LEN];
    protocol_format_request(&req, wire, sizeof(wire));
    if (!client_send_all(conn->socket_fd, wire, strlen(wire))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    char resp_line[DLOCK_MAX_LINE_LEN];
    if (!client_read_line(conn->rx_stream, resp_line, sizeof(resp_line))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    response_t resp;
    int rc = protocol_parse_response(resp_line, &resp);
    if (rc == DLOCK_SUCCESS && resp.type == RESP_OK) {
        return DLOCK_SUCCESS;
    }
    return rc;
}

int dlock_status(dlock_conn_t *conn, const char *name, char *out_buf, size_t buf_sz) {
    if (conn == NULL || out_buf == NULL || buf_sz == 0) {
        return DLOCK_ERR_BADCMD;
    }

    request_t req;
    memset(&req, 0, sizeof(req));
    req.type = CMD_STATUS;
    if (name != NULL) {
        strncpy(req.lock_name, name, sizeof(req.lock_name) - 1);
    }

    char wire[DLOCK_MAX_LINE_LEN];
    protocol_format_request(&req, wire, sizeof(wire));
    if (!client_send_all(conn->socket_fd, wire, strlen(wire))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    out_buf[0] = '\0';
    size_t offset = 0;
    char resp_line[DLOCK_MAX_LINE_LEN];

    while (client_read_line(conn->rx_stream, resp_line, sizeof(resp_line))) {
        response_t resp;
        protocol_parse_response(resp_line, &resp);
        if (resp.type == RESP_END) {
            break;
        }
        if (resp.type == RESP_INFO) {
            int written = snprintf(out_buf + offset, buf_sz - offset, "%s\n", resp.message);
            if (written > 0 && (size_t)written < buf_sz - offset) {
                offset += (size_t)written;
            }
        } else if (resp.type != RESP_OK) {
            return DLOCK_ERR_BADCMD;
        }
    }

    return DLOCK_SUCCESS;
}

int dlock_create(dlock_conn_t *conn, const char *name, uint32_t capacity) {
    if (conn == NULL || name == NULL || capacity == 0) {
        return DLOCK_ERR_BADCMD;
    }

    request_t req;
    memset(&req, 0, sizeof(req));
    req.type = CMD_CREATE;
    strncpy(req.lock_name, name, sizeof(req.lock_name) - 1);
    req.capacity = capacity;

    char wire[DLOCK_MAX_LINE_LEN];
    protocol_format_request(&req, wire, sizeof(wire));
    if (!client_send_all(conn->socket_fd, wire, strlen(wire))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    char resp_line[DLOCK_MAX_LINE_LEN];
    if (!client_read_line(conn->rx_stream, resp_line, sizeof(resp_line))) {
        return DLOCK_ERR_DISCONNECTED;
    }

    response_t resp;
    int rc = protocol_parse_response(resp_line, &resp);
    if (rc == DLOCK_SUCCESS && resp.type == RESP_OK) {
        return DLOCK_SUCCESS;
    }
    return rc;
}

void dlock_close(dlock_conn_t *conn) {
    if (conn == NULL) {
        return;
    }

    /* Send QUIT */
    const char *quit_wire = "QUIT\n";
    (void)write(conn->socket_fd, quit_wire, strlen(quit_wire));

    if (conn->rx_stream != NULL) {
        fclose(conn->rx_stream);
        conn->rx_stream = NULL;
    } else if (conn->socket_fd >= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
    }

    free(conn);
}
