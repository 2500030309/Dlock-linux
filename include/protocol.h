/**
 * @file protocol.h
 * @brief Wire protocol definition, request parser, and response builder.
 *
 * Implements a line-based ASCII protocol terminated by '\n'.
 * Handles defensive parsing, input validation, and formatting for both
 * client and server sides. Maximum line length is 256 bytes.
 */

#ifndef DLOCK_PROTOCOL_H
#define DLOCK_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "common.h"

typedef enum {
    CMD_UNKNOWN = 0,
    CMD_HELLO,
    CMD_LOCK,
    CMD_TRYLOCK,
    CMD_UNLOCK,
    CMD_RENEW,
    CMD_STATUS,
    CMD_CREATE,
    CMD_QUIT
} command_type_t;

typedef struct {
    command_type_t type;
    char client_id[DLOCK_MAX_CLIENT_ID_LEN + 1];
    char lock_name[DLOCK_MAX_NAME_LEN + 1];
    uint32_t timeout_ms;
    uint32_t lease_ms;
    uint64_t token;
    uint32_t capacity;
} request_t;

typedef enum {
    RESP_OK = 0,
    RESP_OK_TOKEN,
    RESP_ERR_TIMEOUT,
    RESP_ERR_BUSY,
    RESP_ERR_DEADLOCK,
    RESP_ERR_NOTOWNER,
    RESP_ERR_EXPIRED,
    RESP_ERR_BADCMD,
    RESP_ERR_NOHELLO,
    RESP_INFO,
    RESP_END
} response_type_t;

typedef struct {
    response_type_t type;
    uint64_t token;
    char message[DLOCK_MAX_LINE_LEN];
} response_t;

/**
 * @brief Validate if a client ID contains only valid characters [A-Za-z0-9_-].
 *
 * @param id Client ID string.
 * @return bool True if valid, false otherwise.
 */
bool protocol_validate_id(const char *id);

/**
 * @brief Validate if a lock name contains only valid characters [A-Za-z0-9_.-].
 *
 * @param name Lock name string.
 * @return bool True if valid, false otherwise.
 */
bool protocol_validate_name(const char *name);

/**
 * @brief Parse a raw input line into a request_t structure.
 *
 * Enforces defensive parsing: truncates or rejects oversized lines,
 * validates token counts, and ensures numerical parameters are within bounds.
 *
 * @param line Raw null-terminated input string (may contain trailing \r or \n).
 * @param req Pointer to output request_t struct.
 * @param err_msg Output buffer for rejection explanation on error.
 * @param err_sz Size of err_msg buffer.
 * @return int DLOCK_SUCCESS (0) on success, negative error code otherwise.
 */
int protocol_parse_request(const char *line, request_t *req, char *err_msg, size_t err_sz);

/**
 * @brief Parse a server response line into a response_t structure.
 *
 * Used by client library and test harnesses.
 *
 * @param line Raw response line received from server.
 * @param resp Pointer to output response_t struct.
 * @return int DLOCK_SUCCESS on success, negative error code otherwise.
 */
int protocol_parse_response(const char *line, response_t *resp);

/**
 * @brief Format a response structure into a newline-terminated wire string.
 *
 * @param resp Input response structure.
 * @param buf Output string buffer.
 * @param buf_sz Size of output buffer.
 * @return int Number of bytes written, or -1 on buffer overflow.
 */
int protocol_format_response(const response_t *resp, char *buf, size_t buf_sz);

/**
 * @brief Format a client command into a wire line ready to send over TCP.
 *
 * @param req Input request structure.
 * @param buf Output string buffer.
 * @param buf_sz Size of output buffer.
 * @return int Number of bytes written, or -1 on buffer overflow.
 */
int protocol_format_request(const request_t *req, char *buf, size_t buf_sz);

#endif /* DLOCK_PROTOCOL_H */
