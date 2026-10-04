#ifndef DLOCK_PROTOCOL_H
#define DLOCK_PROTOCOL_H

#include <stdint.h>
#include <sys/types.h>

#define PROTOCOL_MAX_CLIENT_ID 32
#define PROTOCOL_MAX_MESSAGE 256

typedef enum {
    REQ_LOCK = 1,
    REQ_UNLOCK,
    REQ_STATUS,
    REQ_PING,
    REQ_SHUTDOWN
} request_type_t;

typedef enum {
    RESP_GRANTED = 1,
    RESP_WAITING,
    RESP_RELEASED,
    RESP_DENIED,
    RESP_STATUS_OK,
    RESP_PONG,
    RESP_SHUTDOWN_OK,
    RESP_ERROR
} response_type_t;

typedef struct {
    request_type_t type;
    char client_id[PROTOCOL_MAX_CLIENT_ID];
    pid_t client_pid;
    char reply_fifo[128];
} dlock_request_t;

typedef struct {
    response_type_t type;
    char owner_id[PROTOCOL_MAX_CLIENT_ID];
    pid_t owner_pid;
    int queue_length;
    int total_grants;
    char message[PROTOCOL_MAX_MESSAGE];
} dlock_response_t;

const char *request_type_to_string(request_type_t type);
const char *response_type_to_string(response_type_t type);

#endif /* DLOCK_PROTOCOL_H */
