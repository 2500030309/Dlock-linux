/**
 * @file logger.c
 * @brief Implementation of thread-safe timestamped logger.
 *
 * Implements centralized logging for DLock. A dedicated pthread_mutex_t
 * serializes access across all worker and management threads.
 */

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static pthread_mutex_t s_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *s_log_file = NULL;
static log_level_t s_min_level = LOG_LEVEL_INFO;
static bool s_initialized = false;

static const char *level_to_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "LOG  ";
    }
}

int logger_init(const char *log_file, log_level_t level) {
    pthread_mutex_lock(&s_log_mutex);
    s_min_level = level;

    if (s_log_file != NULL && s_log_file != stdout && s_log_file != stderr) {
        fclose(s_log_file);
        s_log_file = NULL;
    }

    if (log_file != NULL && strlen(log_file) > 0) {
        s_log_file = fopen(log_file, "a");
        if (s_log_file == NULL) {
            pthread_mutex_unlock(&s_log_mutex);
            return -1;
        }
    }

    s_initialized = true;
    pthread_mutex_unlock(&s_log_mutex);
    return 0;
}

void logger_set_level(log_level_t level) {
    pthread_mutex_lock(&s_log_mutex);
    s_min_level = level;
    pthread_mutex_unlock(&s_log_mutex);
}

log_level_t logger_parse_level(const char *str) {
    if (str == NULL) {
        return LOG_LEVEL_INFO;
    }
    if (strcasecmp(str, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(str, "INFO") == 0)  return LOG_LEVEL_INFO;
    if (strcasecmp(str, "WARN") == 0)  return LOG_LEVEL_WARN;
    if (strcasecmp(str, "ERROR") == 0) return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

void logger_log(log_level_t level, const char *fmt, ...) {
    if (level < s_min_level || level >= LOG_LEVEL_NONE) {
        return;
    }

    /* Format timestamp: YYYY-MM-DD HH:MM:SS.mmm */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
    int millis = (int)(ts.tv_nsec / 1000000L);

    pthread_mutex_lock(&s_log_mutex);

    /* Print to stdout */
    fprintf(stdout, "[%s.%03d] [%s] ", time_str, millis, level_to_string(level));
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fprintf(stdout, "\n");
    fflush(stdout);

    /* Print to log file if configured */
    if (s_log_file != NULL) {
        fprintf(s_log_file, "[%s.%03d] [%s] ", time_str, millis, level_to_string(level));
        va_start(args, fmt);
        vfprintf(s_log_file, fmt, args);
        va_end(args);
        fprintf(s_log_file, "\n");
        fflush(s_log_file);
    }

    pthread_mutex_unlock(&s_log_mutex);
}

void logger_cleanup(void) {
    pthread_mutex_lock(&s_log_mutex);
    if (s_log_file != NULL && s_log_file != stdout && s_log_file != stderr) {
        fclose(s_log_file);
        s_log_file = NULL;
    }
    s_initialized = false;
    pthread_mutex_unlock(&s_log_mutex);
}
