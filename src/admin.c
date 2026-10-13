/**
 * @file admin.c
 * @brief Named pipe (FIFO) administrative control channel implementation.
 *
 * Exposes a control channel at /tmp/dlock.ctl to receive local administrative
 * commands (STATS, DUMP, LOG <level>, SHUTDOWN). Runs in a dedicated background
 * pthread and cleans up the FIFO file upon shutdown.
 */

#include "admin.h"
#include "signals.h"
#include "stats.h"
#include "lock_table.h"
#include "logger.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdatomic.h>

static char s_fifo_path[256] = {0};
static pthread_t s_admin_thread;
static atomic_bool s_running = false;
static int s_fifo_fd = -1;

static void *admin_worker(void *arg) {
    (void)arg;
    logger_log(LOG_LEVEL_INFO, "Admin FIFO listener active on '%s'", s_fifo_path);

    /* Opening with O_RDWR prevents EOF loops when external writers close */
    s_fifo_fd = open(s_fifo_path, O_RDWR);
    if (s_fifo_fd < 0) {
        logger_log(LOG_LEVEL_ERROR, "Failed to open admin FIFO '%s': %s", s_fifo_path, strerror(errno));
        return NULL;
    }

    FILE *fp = fdopen(s_fifo_fd, "r");
    if (fp == NULL) {
        close(s_fifo_fd);
        s_fifo_fd = -1;
        return NULL;
    }

    char line[DLOCK_MAX_LINE_LEN];
    while (atomic_load(&s_running) && fgets(line, sizeof(line), fp) != NULL) {
        /* Strip line endings */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
            line[len - 1] = '\0';
            len--;
        }

        if (len == 0) continue;

        logger_log(LOG_LEVEL_INFO, "Admin command received: '%s'", line);

        if (strcasecmp(line, "STATS") == 0) {
            stats_dump(stdout);
        } else if (strcasecmp(line, "DUMP") == 0) {
            char dump_buf[4096];
            lock_table_get_status(NULL, dump_buf, sizeof(dump_buf));
            printf("=== Lock Table Dump ===\n%s========================\n", dump_buf);
            fflush(stdout);
        } else if (strncasecmp(line, "LOG ", 4) == 0) {
            log_level_t lvl = logger_parse_level(line + 4);
            logger_set_level(lvl);
            logger_log(LOG_LEVEL_INFO, "Log level updated to %s", line + 4);
        } else if (strcasecmp(line, "SHUTDOWN") == 0) {
            logger_log(LOG_LEVEL_INFO, "Admin channel initiated server shutdown");
            signals_trigger_shutdown();
            break;
        } else {
            logger_log(LOG_LEVEL_WARN, "Unknown admin command: '%s'", line);
        }
    }

    fclose(fp);
    s_fifo_fd = -1;
    logger_log(LOG_LEVEL_DEBUG, "Admin FIFO listener stopped");
    return NULL;
}

int admin_init(const char *fifo_path) {
    if (fifo_path == NULL) {
        fifo_path = DLOCK_ADMIN_FIFO_PATH;
    }

    strncpy(s_fifo_path, fifo_path, sizeof(s_fifo_path) - 1);
    s_fifo_path[sizeof(s_fifo_path) - 1] = '\0';

    /* Unlink stale FIFO if exists */
    unlink(s_fifo_path);

    if (mkfifo(s_fifo_path, 0666) < 0) {
        logger_log(LOG_LEVEL_ERROR, "Failed to create admin FIFO '%s': %s", s_fifo_path, strerror(errno));
        return -1;
    }

    return 0;
}

int admin_start(void) {
    atomic_store(&s_running, true);
    if (pthread_create(&s_admin_thread, NULL, admin_worker, NULL) != 0) {
        atomic_store(&s_running, false);
        logger_log(LOG_LEVEL_ERROR, "Failed to spawn admin thread");
        return -1;
    }
    return 0;
}

void admin_stop(void) {
    if (!atomic_load(&s_running)) {
        return;
    }
    atomic_store(&s_running, false);
    /* Wake the thread by opening and writing a newline if blocked */
    int wake_fd = open(s_fifo_path, O_WRONLY | O_NONBLOCK);
    if (wake_fd >= 0) {
        (void)write(wake_fd, "\n", 1);
        close(wake_fd);
    }
    pthread_join(s_admin_thread, NULL);
}

void admin_cleanup(void) {
    admin_stop();
    if (s_fifo_path[0] != '\0') {
        unlink(s_fifo_path);
        s_fifo_path[0] = '\0';
    }
}
