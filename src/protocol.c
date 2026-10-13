/**
 * @file protocol.c
 * @brief Implementation of DLock wire protocol parser and formatter.
 *
 * Implements defensive line-based parsing for client commands and server responses.
 * Never trusts input lengths or contents, ensures strict bounds checking, and
 * prevents buffer overflows.
 */

#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

bool protocol_validate_id(const char *id) {
    if (id == NULL) {
        return false;
    }
    size_t len = strlen(id);
    if (len == 0 || len > DLOCK_MAX_CLIENT_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

bool protocol_validate_name(const char *name) {
    if (name == NULL) {
        return false;
    }
    size_t len = strlen(name);
    if (len == 0 || len > DLOCK_MAX_NAME_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

static void trim_line_endings(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' || isspace((unsigned char)str[len - 1]))) {
        str[len - 1] = '\0';
        len--;
    }
}

int protocol_parse_request(const char *line, request_t *req, char *err_msg, size_t err_sz) {
    if (line == NULL || req == NULL) {
        if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "NULL pointer passed");
        return DLOCK_ERR_BADCMD;
    }

    memset(req, 0, sizeof(*req));
    char buf[DLOCK_MAX_LINE_LEN + 2];
    size_t len = strlen(line);
    if (len > DLOCK_MAX_LINE_LEN) {
        if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Command line exceeds maximum length");
        return DLOCK_ERR_BADCMD;
    }

    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim_line_endings(buf);

    if (buf[0] == '\0') {
        if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Empty command");
        return DLOCK_ERR_BADCMD;
    }

    /* Tokenize into up to 5 tokens */
    char *tokens[5] = {0};
    int token_count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(buf, " \t", &saveptr);
    while (token != NULL && token_count < 5) {
        tokens[token_count++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    if (token_count == 0) {
        if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "No tokens found");
        return DLOCK_ERR_BADCMD;
    }

    const char *cmd = tokens[0];

    /* HELLO <client_id> */
    if (strcasecmp(cmd, "HELLO") == 0) {
        if (token_count < 2) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Missing client_id in HELLO");
            return DLOCK_ERR_BADCMD;
        }
        if (!protocol_validate_id(tokens[1])) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid client_id format");
            return DLOCK_ERR_BADCMD;
        }
        req->type = CMD_HELLO;
        strncpy(req->client_id, tokens[1], sizeof(req->client_id) - 1);
        return DLOCK_SUCCESS;
    }

    /* LOCK <name> <timeout_ms> [lease_ms] */
    if (strcasecmp(cmd, "LOCK") == 0) {
        if (token_count < 3) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Usage: LOCK <name> <timeout_ms> [lease_ms]");
            return DLOCK_ERR_BADCMD;
        }
        if (!protocol_validate_name(tokens[1])) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lock name");
            return DLOCK_ERR_BADCMD;
        }
        char *endptr = NULL;
        unsigned long t_ms = strtoul(tokens[2], &endptr, 10);
        if (*endptr != '\0') {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid timeout_ms");
            return DLOCK_ERR_BADCMD;
        }
        unsigned long l_ms = DLOCK_DEFAULT_LEASE_MS;
        if (token_count >= 4) {
            l_ms = strtoul(tokens[3], &endptr, 10);
            if (*endptr != '\0' || l_ms == 0) {
                if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lease_ms");
                return DLOCK_ERR_BADCMD;
            }
        }
        req->type = CMD_LOCK;
        strncpy(req->lock_name, tokens[1], sizeof(req->lock_name) - 1);
        req->timeout_ms = (uint32_t)t_ms;
        req->lease_ms = (uint32_t)l_ms;
        return DLOCK_SUCCESS;
    }

    /* TRYLOCK <name> [lease_ms] */
    if (strcasecmp(cmd, "TRYLOCK") == 0) {
        if (token_count < 2) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Usage: TRYLOCK <name> [lease_ms]");
            return DLOCK_ERR_BADCMD;
        }
        if (!protocol_validate_name(tokens[1])) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lock name");
            return DLOCK_ERR_BADCMD;
        }
        unsigned long l_ms = DLOCK_DEFAULT_LEASE_MS;
        if (token_count >= 3) {
            char *endptr = NULL;
            l_ms = strtoul(tokens[2], &endptr, 10);
            if (*endptr != '\0' || l_ms == 0) {
                if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lease_ms");
                return DLOCK_ERR_BADCMD;
            }
        }
        req->type = CMD_TRYLOCK;
        strncpy(req->lock_name, tokens[1], sizeof(req->lock_name) - 1);
        req->lease_ms = (uint32_t)l_ms;
        return DLOCK_SUCCESS;
    }

    /* UNLOCK <name> <token> */
    if (strcasecmp(cmd, "UNLOCK") == 0) {
        if (token_count < 3) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Usage: UNLOCK <name> <token>");
            return DLOCK_ERR_BADCMD;
        }
        if (!protocol_validate_name(tokens[1])) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lock name");
            return DLOCK_ERR_BADCMD;
        }
        char *endptr = NULL;
        unsigned long long tok = strtoull(tokens[2], &endptr, 10);
        if (*endptr != '\0' || tok == 0) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid fencing token");
            return DLOCK_ERR_BADCMD;
        }
        req->type = CMD_UNLOCK;
        strncpy(req->lock_name, tokens[1], sizeof(req->lock_name) - 1);
        req->token = (uint64_t)tok;
        return DLOCK_SUCCESS;
    }

    /* RENEW <name> <token> <lease_ms> */
    if (strcasecmp(cmd, "RENEW") == 0) {
        if (token_count < 4) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Usage: RENEW <name> <token> <lease_ms>");
            return DLOCK_ERR_BADCMD;
        }
        if (!protocol_validate_name(tokens[1])) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lock name");
            return DLOCK_ERR_BADCMD;
        }
        char *endptr = NULL;
        unsigned long long tok = strtoull(tokens[2], &endptr, 10);
        if (*endptr != '\0' || tok == 0) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid fencing token");
            return DLOCK_ERR_BADCMD;
        }
        unsigned long l_ms = strtoul(tokens[3], &endptr, 10);
        if (*endptr != '\0' || l_ms == 0) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lease_ms");
            return DLOCK_ERR_BADCMD;
        }
        req->type = CMD_RENEW;
        strncpy(req->lock_name, tokens[1], sizeof(req->lock_name) - 1);
        req->token = (uint64_t)tok;
        req->lease_ms = (uint32_t)l_ms;
        return DLOCK_SUCCESS;
    }

    /* STATUS [name] */
    if (strcasecmp(cmd, "STATUS") == 0) {
        req->type = CMD_STATUS;
        if (token_count >= 2) {
            if (!protocol_validate_name(tokens[1])) {
                if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lock name");
                return DLOCK_ERR_BADCMD;
            }
            strncpy(req->lock_name, tokens[1], sizeof(req->lock_name) - 1);
        }
        return DLOCK_SUCCESS;
    }

    /* CREATE <name> <capacity> */
    if (strcasecmp(cmd, "CREATE") == 0) {
        if (token_count < 3) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Usage: CREATE <name> <capacity>");
            return DLOCK_ERR_BADCMD;
        }
        if (!protocol_validate_name(tokens[1])) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid lock name");
            return DLOCK_ERR_BADCMD;
        }
        char *endptr = NULL;
        unsigned long cap = strtoul(tokens[2], &endptr, 10);
        if (*endptr != '\0' || cap == 0 || cap > DLOCK_MAX_HOLDERS) {
            if (err_msg && err_sz > 0) snprintf(err_msg, err_sz, "Invalid capacity (1..%d)", DLOCK_MAX_HOLDERS);
            return DLOCK_ERR_BADCMD;
        }
        req->type = CMD_CREATE;
        strncpy(req->lock_name, tokens[1], sizeof(req->lock_name) - 1);
        req->capacity = (uint32_t)cap;
        return DLOCK_SUCCESS;
    }

    /* QUIT */
    if (strcasecmp(cmd, "QUIT") == 0) {
        req->type = CMD_QUIT;
        return DLOCK_SUCCESS;
    }

    if (err_msg && err_sz > 0) {
        snprintf(err_msg, err_sz, "Unknown command '%s'", cmd);
    }
    return DLOCK_ERR_BADCMD;
}

int protocol_parse_response(const char *line, response_t *resp) {
    if (line == NULL || resp == NULL) {
        return DLOCK_ERR_BADCMD;
    }

    memset(resp, 0, sizeof(*resp));
    char buf[DLOCK_MAX_LINE_LEN + 2];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim_line_endings(buf);

    if (strncmp(buf, "OK ", 3) == 0) {
        resp->type = RESP_OK_TOKEN;
        char *endptr = NULL;
        resp->token = (uint64_t)strtoull(buf + 3, &endptr, 10);
        return DLOCK_SUCCESS;
    }
    if (strcmp(buf, "OK") == 0) {
        resp->type = RESP_OK;
        return DLOCK_SUCCESS;
    }
    if (strcmp(buf, "ERR TIMEOUT") == 0) {
        resp->type = RESP_ERR_TIMEOUT;
        return DLOCK_ERR_TIMEOUT;
    }
    if (strcmp(buf, "ERR BUSY") == 0) {
        resp->type = RESP_ERR_BUSY;
        return DLOCK_ERR_BUSY;
    }
    if (strcmp(buf, "ERR DEADLOCK") == 0) {
        resp->type = RESP_ERR_DEADLOCK;
        return DLOCK_ERR_DEADLOCK;
    }
    if (strcmp(buf, "ERR NOTOWNER") == 0) {
        resp->type = RESP_ERR_NOTOWNER;
        return DLOCK_ERR_NOTOWNER;
    }
    if (strcmp(buf, "ERR EXPIRED") == 0) {
        resp->type = RESP_ERR_EXPIRED;
        return DLOCK_ERR_EXPIRED;
    }
    if (strncmp(buf, "ERR BADCMD", 10) == 0) {
        resp->type = RESP_ERR_BADCMD;
        if (strlen(buf) > 11) {
            strncpy(resp->message, buf + 11, sizeof(resp->message) - 1);
        }
        return DLOCK_ERR_BADCMD;
    }
    if (strcmp(buf, "ERR NOHELLO") == 0) {
        resp->type = RESP_ERR_NOHELLO;
        return DLOCK_ERR_NOHELLO;
    }
    if (strncmp(buf, "INFO ", 5) == 0) {
        resp->type = RESP_INFO;
        strncpy(resp->message, buf + 5, sizeof(resp->message) - 1);
        return DLOCK_SUCCESS;
    }
    if (strcmp(buf, "END") == 0) {
        resp->type = RESP_END;
        return DLOCK_SUCCESS;
    }

    resp->type = RESP_ERR_BADCMD;
    strncpy(resp->message, buf, sizeof(resp->message) - 1);
    return DLOCK_ERR_BADCMD;
}

int protocol_format_response(const response_t *resp, char *buf, size_t buf_sz) {
    if (resp == NULL || buf == NULL || buf_sz == 0) {
        return -1;
    }

    int written = 0;
    switch (resp->type) {
        case RESP_OK:
            written = snprintf(buf, buf_sz, "OK\n");
            break;
        case RESP_OK_TOKEN:
            written = snprintf(buf, buf_sz, "OK %llu\n", (unsigned long long)resp->token);
            break;
        case RESP_ERR_TIMEOUT:
            written = snprintf(buf, buf_sz, "ERR TIMEOUT\n");
            break;
        case RESP_ERR_BUSY:
            written = snprintf(buf, buf_sz, "ERR BUSY\n");
            break;
        case RESP_ERR_DEADLOCK:
            written = snprintf(buf, buf_sz, "ERR DEADLOCK\n");
            break;
        case RESP_ERR_NOTOWNER:
            written = snprintf(buf, buf_sz, "ERR NOTOWNER\n");
            break;
        case RESP_ERR_EXPIRED:
            written = snprintf(buf, buf_sz, "ERR EXPIRED\n");
            break;
        case RESP_ERR_BADCMD:
            if (resp->message[0] != '\0') {
                written = snprintf(buf, buf_sz, "ERR BADCMD %s\n", resp->message);
            } else {
                written = snprintf(buf, buf_sz, "ERR BADCMD\n");
            }
            break;
        case RESP_ERR_NOHELLO:
            written = snprintf(buf, buf_sz, "ERR NOHELLO\n");
            break;
        case RESP_INFO:
            written = snprintf(buf, buf_sz, "INFO %s\n", resp->message);
            break;
        case RESP_END:
            written = snprintf(buf, buf_sz, "END\n");
            break;
        default:
            written = snprintf(buf, buf_sz, "ERR BADCMD unknown response\n");
            break;
    }

    if (written < 0 || (size_t)written >= buf_sz) {
        return -1;
    }
    return written;
}

int protocol_format_request(const request_t *req, char *buf, size_t buf_sz) {
    if (req == NULL || buf == NULL || buf_sz == 0) {
        return -1;
    }

    int written = 0;
    switch (req->type) {
        case CMD_HELLO:
            written = snprintf(buf, buf_sz, "HELLO %s\n", req->client_id);
            break;
        case CMD_LOCK:
            written = snprintf(buf, buf_sz, "LOCK %s %u %u\n",
                               req->lock_name, req->timeout_ms, req->lease_ms);
            break;
        case CMD_TRYLOCK:
            written = snprintf(buf, buf_sz, "TRYLOCK %s %u\n",
                               req->lock_name, req->lease_ms);
            break;
        case CMD_UNLOCK:
            written = snprintf(buf, buf_sz, "UNLOCK %s %llu\n",
                               req->lock_name, (unsigned long long)req->token);
            break;
        case CMD_RENEW:
            written = snprintf(buf, buf_sz, "RENEW %s %llu %u\n",
                               req->lock_name, (unsigned long long)req->token, req->lease_ms);
            break;
        case CMD_STATUS:
            if (req->lock_name[0] != '\0') {
                written = snprintf(buf, buf_sz, "STATUS %s\n", req->lock_name);
            } else {
                written = snprintf(buf, buf_sz, "STATUS\n");
            }
            break;
        case CMD_CREATE:
            written = snprintf(buf, buf_sz, "CREATE %s %u\n", req->lock_name, req->capacity);
            break;
        case CMD_QUIT:
            written = snprintf(buf, buf_sz, "QUIT\n");
            break;
        default:
            return -1;
    }

    if (written < 0 || (size_t)written >= buf_sz) {
        return -1;
    }
    return written;
}
