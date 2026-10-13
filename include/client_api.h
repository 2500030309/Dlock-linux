/**
 * @file client_api.h
 * @brief C client library for interacting with the DLock server.
 *
 * Provides a synchronous, blocking API for client applications to connect,
 * acquire, release, renew, and query distributed locks. Handles line-based
 * TCP socket I/O, partial reads/writes, and EINTR internally.
 */

#ifndef DLOCK_CLIENT_API_H
#define DLOCK_CLIENT_API_H

#include <stdint.h>
#include <stddef.h>
#include "common.h"

typedef struct dlock_conn dlock_conn_t;

/**
 * @brief Connect to a DLock server and complete the HELLO handshake.
 *
 * @param host Server hostname or IPv4 address (e.g. "127.0.0.1").
 * @param port Server TCP port (e.g. 7070).
 * @param client_id Unique client identifier ([A-Za-z0-9_-]{1,32}).
 * @return dlock_conn_t* Connection handle, or NULL on connection/handshake failure.
 */
dlock_conn_t *dlock_connect(const char *host, int port, const char *client_id);

/**
 * @brief Acquire a named lock, blocking until granted or timeout occurs.
 *
 * @param conn Active connection handle.
 * @param name Lock name.
 * @param timeout_ms Max wait time in ms (0 = wait forever).
 * @param lease_ms Lease TTL in ms (0 defaults to DLOCK_DEFAULT_LEASE_MS).
 * @param out_token Pointer to store the granted 64-bit fencing token.
 * @return int DLOCK_SUCCESS (0) on grant, or negative error code:
 *             DLOCK_ERR_TIMEOUT, DLOCK_ERR_DEADLOCK, DLOCK_ERR_BUSY, DLOCK_ERR_SYSTEM.
 */
int dlock_lock(dlock_conn_t *conn,
               const char *name,
               uint32_t timeout_ms,
               uint32_t lease_ms,
               uint64_t *out_token);

/**
 * @brief Non-blocking attempt to acquire a named lock.
 *
 * @param conn Active connection handle.
 * @param name Lock name.
 * @param lease_ms Lease TTL in ms (0 defaults to DLOCK_DEFAULT_LEASE_MS).
 * @param out_token Pointer to store the granted fencing token.
 * @return int DLOCK_SUCCESS (0) if granted immediately,
 *             DLOCK_ERR_BUSY if already held, or negative error code.
 */
int dlock_trylock(dlock_conn_t *conn,
                  const char *name,
                  uint32_t lease_ms,
                  uint64_t *out_token);

/**
 * @brief Release a held lock using the fencing token.
 *
 * @param conn Active connection handle.
 * @param name Lock name.
 * @param token Fencing token returned when lock was acquired.
 * @return int DLOCK_SUCCESS, DLOCK_ERR_NOTOWNER, DLOCK_ERR_EXPIRED, or DLOCK_ERR_SYSTEM.
 */
int dlock_unlock(dlock_conn_t *conn, const char *name, uint64_t token);

/**
 * @brief Extend the lease TTL of a held lock (heartbeat).
 *
 * @param conn Active connection handle.
 * @param name Lock name.
 * @param token Fencing token held by caller.
 * @param lease_ms Extended lease duration from now in ms.
 * @return int DLOCK_SUCCESS, DLOCK_ERR_NOTOWNER, DLOCK_ERR_EXPIRED, or DLOCK_ERR_SYSTEM.
 */
int dlock_renew(dlock_conn_t *conn, const char *name, uint64_t token, uint32_t lease_ms);

/**
 * @brief Query server lock status and holders.
 *
 * @param conn Active connection handle.
 * @param name Lock name to query, or NULL / empty string for all locks.
 * @param out_buf Buffer to store response text.
 * @param buf_sz Size of out_buf.
 * @return int DLOCK_SUCCESS on success, negative error code on failure.
 */
int dlock_status(dlock_conn_t *conn, const char *name, char *out_buf, size_t buf_sz);

/**
 * @brief Create a lock with an explicit capacity (counting semaphore).
 *
 * @param conn Active connection handle.
 * @param name Lock name.
 * @param capacity Maximum concurrent holders.
 * @return int DLOCK_SUCCESS or negative error code.
 */
int dlock_create(dlock_conn_t *conn, const char *name, uint32_t capacity);

/**
 * @brief Gracefully send QUIT and close client connection.
 *
 * @param conn Connection handle to destroy.
 */
void dlock_close(dlock_conn_t *conn);

#endif /* DLOCK_CLIENT_API_H */
