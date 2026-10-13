/**
 * @file logger.h
 * @brief Thread-safe, timestamped logging module for DLock.
 *
 * Implements formatted logging across different severity levels with
 * thread-safety guaranteed by an internal mutex. Outputs can be directed
 * to standard output and a file.
 */

#ifndef DLOCK_LOGGER_H
#define DLOCK_LOGGER_H

#include <stdio.h>
#include <stdbool.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_NONE  = 4
} log_level_t;

/**
 * @brief Initialize the logger subsystem.
 *
 * @param log_file Optional file path for appending logs, or NULL for stdout only.
 * @param level Minimum log level to emit.
 * @return int 0 on success, -1 on file open failure.
 */
int logger_init(const char *log_file, log_level_t level);

/**
 * @brief Set the active log filtering level at runtime.
 *
 * @param level Target log level.
 */
void logger_set_level(log_level_t level);

/**
 * @brief Parse a text string into a log_level_t.
 *
 * @param str Level string (e.g., "DEBUG", "INFO", "WARN", "ERROR").
 * @return log_level_t Parsed level, defaults to INFO if unrecognized.
 */
log_level_t logger_parse_level(const char *str);

/**
 * @brief Log a formatted message with thread safety and timestamp.
 *
 * @param level Severity level of the log message.
 * @param fmt Printf-style format string.
 * @param ... Variable argument list.
 */
void logger_log(log_level_t level, const char *fmt, ...);

/**
 * @brief Flush and close log descriptors and destroy mutexes.
 */
void logger_cleanup(void);

#endif /* DLOCK_LOGGER_H */
