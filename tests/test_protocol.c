/**
 * @file test_protocol.c
 * @brief Unit tests for protocol parsing, validation, and serialization.
 */

#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

static void test_id_validation(void) {
    TEST_ASSERT(protocol_validate_id("alice"), "valid simple id");
    TEST_ASSERT(protocol_validate_id("worker-01_prod"), "valid complex id");
    TEST_ASSERT(!protocol_validate_id(""), "empty id rejected");
    TEST_ASSERT(!protocol_validate_id("bad id spaces"), "spaces rejected");
    TEST_ASSERT(!protocol_validate_id("alice$123"), "special chars rejected");
    TEST_ASSERT(!protocol_validate_id("a_very_long_client_id_exceeding_the_thirty_two_character_limit_here"), "oversized id rejected");
}

static void test_name_validation(void) {
    TEST_ASSERT(protocol_validate_name("database"), "valid name");
    TEST_ASSERT(protocol_validate_name("user.account_lock-v2"), "valid chars");
    TEST_ASSERT(!protocol_validate_name(""), "empty name rejected");
    TEST_ASSERT(!protocol_validate_name("has spaces"), "space rejected");
    TEST_ASSERT(!protocol_validate_name("name/slash"), "slash rejected");
}

static void test_parse_requests(void) {
    request_t req;
    char err[64];

    /* HELLO */
    TEST_ASSERT(protocol_parse_request("HELLO worker_1\n", &req, err, sizeof(err)) == DLOCK_SUCCESS, "HELLO parse");
    TEST_ASSERT(req.type == CMD_HELLO, "type HELLO");
    TEST_ASSERT(strcmp(req.client_id, "worker_1") == 0, "client_id match");

    /* LOCK */
    TEST_ASSERT(protocol_parse_request("LOCK orders 5000 12000\n", &req, err, sizeof(err)) == DLOCK_SUCCESS, "LOCK parse");
    TEST_ASSERT(req.type == CMD_LOCK, "type LOCK");
    TEST_ASSERT(strcmp(req.lock_name, "orders") == 0, "lock_name match");
    TEST_ASSERT(req.timeout_ms == 5000, "timeout match");
    TEST_ASSERT(req.lease_ms == 12000, "lease match");

    /* TRYLOCK */
    TEST_ASSERT(protocol_parse_request("TRYLOCK orders 3000\r\n", &req, err, sizeof(err)) == DLOCK_SUCCESS, "TRYLOCK parse");
    TEST_ASSERT(req.type == CMD_TRYLOCK, "type TRYLOCK");
    TEST_ASSERT(req.lease_ms == 3000, "lease match");

    /* UNLOCK */
    TEST_ASSERT(protocol_parse_request("UNLOCK orders 1042\n", &req, err, sizeof(err)) == DLOCK_SUCCESS, "UNLOCK parse");
    TEST_ASSERT(req.type == CMD_UNLOCK, "type UNLOCK");
    TEST_ASSERT(req.token == 1042, "token match");

    /* RENEW */
    TEST_ASSERT(protocol_parse_request("RENEW orders 1042 15000\n", &req, err, sizeof(err)) == DLOCK_SUCCESS, "RENEW parse");
    TEST_ASSERT(req.type == CMD_RENEW, "type RENEW");
    TEST_ASSERT(req.lease_ms == 15000, "renew lease match");

    /* STATUS */
    TEST_ASSERT(protocol_parse_request("STATUS orders\n", &req, err, sizeof(err)) == DLOCK_SUCCESS, "STATUS parse");
    TEST_ASSERT(req.type == CMD_STATUS, "type STATUS");

    /* CREATE */
    TEST_ASSERT(protocol_parse_request("CREATE sema 4\n", &req, err, sizeof(err)) == DLOCK_SUCCESS, "CREATE parse");
    TEST_ASSERT(req.type == CMD_CREATE, "type CREATE");
    TEST_ASSERT(req.capacity == 4, "capacity match");

    /* QUIT */
    TEST_ASSERT(protocol_parse_request("QUIT\n", &req, err, sizeof(err)) == DLOCK_SUCCESS, "QUIT parse");
    TEST_ASSERT(req.type == CMD_QUIT, "type QUIT");

    /* Malformed inputs */
    TEST_ASSERT(protocol_parse_request("INVALID_CMD", &req, err, sizeof(err)) != DLOCK_SUCCESS, "bad cmd rejected");
    TEST_ASSERT(protocol_parse_request("LOCK missing_params", &req, err, sizeof(err)) != DLOCK_SUCCESS, "missing params");
    TEST_ASSERT(protocol_parse_request("LOCK name not_a_number", &req, err, sizeof(err)) != DLOCK_SUCCESS, "not a number");

    /* Oversized input */
    char huge[512];
    memset(huge, 'A', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    TEST_ASSERT(protocol_parse_request(huge, &req, err, sizeof(err)) != DLOCK_SUCCESS, "oversized rejected");
}

static void test_response_formatting_and_parsing(void) {
    response_t resp = {
        .type = RESP_OK_TOKEN,
        .token = 987654321ULL
    };
    char wire[DLOCK_MAX_LINE_LEN];
    TEST_ASSERT(protocol_format_response(&resp, wire, sizeof(wire)) > 0, "format OK token");
    TEST_ASSERT(strcmp(wire, "OK 987654321\n") == 0, "formatted match");

    response_t parsed;
    TEST_ASSERT(protocol_parse_response(wire, &parsed) == DLOCK_SUCCESS, "parse OK token");
    TEST_ASSERT(parsed.type == RESP_OK_TOKEN, "type OK token");
    TEST_ASSERT(parsed.token == 987654321ULL, "parsed token match");

    /* Parse ERR responses */
    TEST_ASSERT(protocol_parse_response("ERR TIMEOUT\n", &parsed) == DLOCK_ERR_TIMEOUT, "parse ERR TIMEOUT");
    TEST_ASSERT(protocol_parse_response("ERR BUSY\n", &parsed) == DLOCK_ERR_BUSY, "parse ERR BUSY");
    TEST_ASSERT(protocol_parse_response("ERR DEADLOCK\n", &parsed) == DLOCK_ERR_DEADLOCK, "parse ERR DEADLOCK");
    TEST_ASSERT(protocol_parse_response("ERR NOTOWNER\n", &parsed) == DLOCK_ERR_NOTOWNER, "parse ERR NOTOWNER");
    TEST_ASSERT(protocol_parse_response("ERR EXPIRED\n", &parsed) == DLOCK_ERR_EXPIRED, "parse ERR EXPIRED");
}

int main(void) {
    printf("Running protocol unit tests...\n");
    test_id_validation();
    test_name_validation();
    test_parse_requests();
    test_response_formatting_and_parsing();
    printf("test_protocol: ALL PASS\n");
    return EXIT_SUCCESS;
}
