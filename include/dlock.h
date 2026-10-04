#ifndef DLOCK_H
#define DLOCK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define DLOCK_VERSION "1.2.0"
#define DEFAULT_LOG_PATH "runtime/logs/dlock.log"
#define DEFAULT_FIFO_PATH "runtime/dlock.fifo"
#define FALLBACK_FIFO_PATH "/tmp/dlock_%u.fifo"
#define SHARED_RESOURCE_PATH "runtime/data/shared_resource.txt"
#define FALLBACK_RESOURCE_PATH "/tmp/dlock_resource_%u.txt"

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"

void print_banner(void);
void print_usage(const char *prog_name);
void dlock_log(const char *format, ...);
const char *resolve_fifo_path(void);
const char *resolve_resource_path(void);
int is_interactive_tty(void);

#endif /* DLOCK_H */
