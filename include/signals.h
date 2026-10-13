/**
 * @file signals.h
 * @brief Async-signal-safe signal handling using the self-pipe trick.
 *
 * Configures signal actions via POSIX sigaction:
 * - SIGINT / SIGTERM: writes a notification byte to an anonymous pipe to wake
 *   the main poll() loop cleanly.
 * - SIGPIPE: ignored to allow graceful handling of closed client sockets.
 * - SIGUSR1: triggers an internal diagnostic dump of the lock table and stats.
 */

#ifndef DLOCK_SIGNALS_H
#define DLOCK_SIGNALS_H

#include <stdbool.h>

/**
 * @brief Initialize the self-pipe and install sigaction handlers.
 *
 * @return int 0 on success, -1 on pipe or sigaction failure.
 */
int signals_init(void);

/**
 * @brief Get the read end file descriptor of the self-pipe.
 *
 * Used by poll() in the main server loop to detect shutdown signals.
 *
 * @return int File descriptor for reading.
 */
int signals_get_pipe_read_fd(void);

/**
 * @brief Check if SIGUSR1 was signaled.
 *
 * @return bool True if dump requested.
 */
bool signals_check_dump_requested(void);

/**
 * @brief Trigger shutdown programmatically (e.g. from admin FIFO).
 */
void signals_trigger_shutdown(void);

/**
 * @brief Close pipe descriptors and restore signal handlers.
 */
void signals_cleanup(void);

#endif /* DLOCK_SIGNALS_H */
