/**
 * @file wal.h
 * @brief Append-only write-ahead log (WAL) for fencing token persistence.
 *
 * Persists lock lifecycle events (GRANT, RELEASE, EXPIRE) with durable fsync()
 * so that after a server restart, fencing tokens remain strictly monotonic and
 * never repeat or move backwards.
 */

#ifndef DLOCK_WAL_H
#define DLOCK_WAL_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the write-ahead log module.
 *
 * Opens or creates the append-only log file in O_APPEND mode.
 *
 * @param path File system path to the WAL file.
 * @return int 0 on success, -1 on open failure.
 */
int wal_init(const char *path);

/**
 * @brief Read an existing WAL file and recover the highest assigned fencing token.
 *
 * Scans all complete records safely and returns the highest token encountered.
 *
 * @param path File system path to WAL.
 * @param highest_token Output pointer receiving recovered maximum token.
 * @return int 0 on success (or empty log), -1 on read error.
 */
int wal_recover(const char *path, uint64_t *highest_token);

/**
 * @brief Append an event record and flush to disk via fsync().
 *
 * Record format: "<timestamp_ms> <event> <lock_name> <client_id> <token>\n"
 *
 * @param event Event type string (e.g. "GRANT", "RELEASE", "EXPIRE").
 * @param lock_name Name of the affected lock.
 * @param client_id Identifier of the interacting client.
 * @param token Associated fencing token.
 * @return int 0 on success, -1 on write/fsync failure.
 */
int wal_log_event(const char *event, const char *lock_name, const char *client_id, uint64_t token);

/**
 * @brief Flush and close the write-ahead log file descriptor.
 */
void wal_close(void);

#endif /* DLOCK_WAL_H */
