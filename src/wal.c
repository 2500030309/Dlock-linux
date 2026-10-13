/**
 * @file wal.c
 * @brief Append-only write-ahead log (WAL) implementation.
 *
 * Persists lock events to durable storage using POSIX syscalls:
 * open(), write(), fsync(), and close(). Provides crash recovery by scanning
 * existing logs on startup to recover the highest monotonically increasing
 * fencing token, ensuring tokens never repeat across server lifetimes.
 */

#include "wal.h"
#include "logger.h"
#include "common.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>

static int s_wal_fd = -1;
static pthread_mutex_t s_wal_mutex = PTHREAD_MUTEX_INITIALIZER;

int wal_init(const char *path) {
    if (path == NULL) {
        return -1;
    }

    pthread_mutex_lock(&s_wal_mutex);
    if (s_wal_fd >= 0) {
        close(s_wal_fd);
        s_wal_fd = -1;
    }

    s_wal_fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (s_wal_fd < 0) {
        logger_log(LOG_LEVEL_ERROR, "Failed to open WAL file '%s': %s", path, strerror(errno));
        pthread_mutex_unlock(&s_wal_mutex);
        return -1;
    }

    pthread_mutex_unlock(&s_wal_mutex);
    logger_log(LOG_LEVEL_INFO, "WAL initialized at '%s'", path);
    return 0;
}

int wal_recover(const char *path, uint64_t *highest_token) {
    if (path == NULL || highest_token == NULL) {
        return -1;
    }

    *highest_token = 0;
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        if (errno == ENOENT) {
            /* Log does not exist yet; fresh start */
            return 0;
        }
        logger_log(LOG_LEVEL_ERROR, "Failed to open WAL for recovery '%s': %s", path, strerror(errno));
        return -1;
    }

    char line[DLOCK_MAX_LINE_LEN + 32];
    uint64_t max_tok = 0;
    size_t line_no = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        line_no++;
        /* Check if line has newline (complete record) */
        size_t len = strlen(line);
        if (len == 0 || line[len - 1] != '\n') {
            /* Partial / truncated last record; stop recovery */
            logger_log(LOG_LEVEL_WARN, "WAL recovery: partial record encountered at line %zu; skipping remainder", line_no);
            break;
        }

        /* Parse: <timestamp> <event> <lock> <client> <token> */
        unsigned long long ts = 0;
        char event[32] = {0};
        char lock_name[DLOCK_MAX_NAME_LEN + 1] = {0};
        char client_id[DLOCK_MAX_CLIENT_ID_LEN + 1] = {0};
        unsigned long long tok = 0;

        int fields = sscanf(line, "%llu %31s %64s %32s %llu",
                            &ts, event, lock_name, client_id, &tok);
        if (fields == 5) {
            if (tok > max_tok) {
                max_tok = tok;
            }
        }
    }

    fclose(f);
    *highest_token = max_tok;
    logger_log(LOG_LEVEL_INFO, "WAL recovered highest fencing token: %llu", (unsigned long long)max_tok);
    return 0;
}

int wal_log_event(const char *event, const char *lock_name, const char *client_id, uint64_t token) {
    if (event == NULL || lock_name == NULL || client_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&s_wal_mutex);
    if (s_wal_fd < 0) {
        pthread_mutex_unlock(&s_wal_mutex);
        return -1;
    }

    char record[DLOCK_MAX_LINE_LEN];
    uint64_t now = current_realtime_ms();
    int len = snprintf(record, sizeof(record), "%llu %s %s %s %llu\n",
                       (unsigned long long)now, event, lock_name, client_id, (unsigned long long)token);
    if (len <= 0 || (size_t)len >= sizeof(record)) {
        pthread_mutex_unlock(&s_wal_mutex);
        return -1;
    }

    /* Write complete record */
    const char *ptr = record;
    size_t remaining = (size_t)len;
    while (remaining > 0) {
        ssize_t written = write(s_wal_fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            logger_log(LOG_LEVEL_ERROR, "WAL write failed: %s", strerror(errno));
            pthread_mutex_unlock(&s_wal_mutex);
            return -1;
        }
        ptr += written;
        remaining -= (size_t)written;
    }

    /* Ensure durability to disk */
    if (fsync(s_wal_fd) < 0) {
        logger_log(LOG_LEVEL_ERROR, "WAL fsync failed: %s", strerror(errno));
        pthread_mutex_unlock(&s_wal_mutex);
        return -1;
    }

    pthread_mutex_unlock(&s_wal_mutex);
    return 0;
}

void wal_close(void) {
    pthread_mutex_lock(&s_wal_mutex);
    if (s_wal_fd >= 0) {
        fsync(s_wal_fd);
        close(s_wal_fd);
        s_wal_fd = -1;
    }
    pthread_mutex_unlock(&s_wal_mutex);
}
