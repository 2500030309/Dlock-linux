/**
 * @file admin.h
 * @brief Named pipe (FIFO) administrative control channel for DLock.
 *
 * Creates a FIFO at /tmp/dlock.ctl and runs a dedicated listener thread.
 * System operators can send commands without network access:
 *   - "STATS"    : Write current statistics to log / stdout
 *   - "DUMP"     : Output lock table snapshot to log / stdout
 *   - "LOG <lvl>": Change log level dynamically (DEBUG, INFO, WARN, ERROR)
 *   - "SHUTDOWN" : Instruct server to gracefully shutdown
 */

#ifndef DLOCK_ADMIN_H
#define DLOCK_ADMIN_H

/**
 * @brief Create the admin FIFO and initialize synchronization structures.
 *
 * @param fifo_path Filesystem path for the FIFO (e.g. "/tmp/dlock.ctl").
 * @return int 0 on success, -1 on mkfifo failure.
 */
int admin_init(const char *fifo_path);

/**
 * @brief Start the background administration reader thread.
 *
 * @return int 0 on success, -1 on thread creation error.
 */
int admin_start(void);

/**
 * @brief Stop the background admin thread.
 */
void admin_stop(void);

/**
 * @brief Remove the FIFO file from filesystem and release memory.
 */
void admin_cleanup(void);

#endif /* DLOCK_ADMIN_H */
