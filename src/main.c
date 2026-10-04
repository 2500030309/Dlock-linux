#include "dlock.h"
#include "protocol.h"
#include "process.h"
#include "ipc.h"
#include "memory.h"
#include "filesystem.h"
#include "concurrency.h"
#include "scheduler.h"
#include "monitor.h"

#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>

int is_interactive_tty(void) {
    return isatty(STDIN_FILENO);
}

const char *resolve_fifo_path(void) {
    static char resolved[256];
    struct stat st;

    if (stat("runtime", &st) == 0 && S_ISDIR(st.st_mode)) {
        const char *test_path = "runtime/.fifo_test";
        if (mkfifo(test_path, 0666) == 0) {
            unlink(test_path);
            return DEFAULT_FIFO_PATH;
        }
    }
    snprintf(resolved, sizeof(resolved), FALLBACK_FIFO_PATH, (unsigned int)getuid());
    return resolved;
}

const char *resolve_resource_path(void) {
    static char res_path[256];
    struct stat st;

    if (stat("runtime/data", &st) == 0 && S_ISDIR(st.st_mode)) {
        return SHARED_RESOURCE_PATH;
    }
    snprintf(res_path, sizeof(res_path), FALLBACK_RESOURCE_PATH, (unsigned int)getuid());
    return res_path;
}

void dlock_log(const char *format, ...) {
    FILE *fp = fopen(DEFAULT_LOG_PATH, "a");
    if (!fp) {
        char fallback_log[128];
        snprintf(fallback_log, sizeof(fallback_log), "/tmp/dlock_%u.log", (unsigned int)getuid());
        fp = fopen(fallback_log, "a");
        if (!fp) return;
    }

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(fp, "[%s] [PID:%d] ", time_str, getpid());

    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}

void print_banner(void) {
    printf("%s%s", COLOR_CYAN, COLOR_BOLD);
    printf("===============================================================\n");
    printf("  DLOCK v%s - Distributed Mutual-Exclusion Service\n", DLOCK_VERSION);
    printf("  High-Performance Systems Programming in C (Ubuntu / Linux)\n");
    printf("===============================================================\n");
    printf("%s", COLOR_RESET);
}

void print_usage(const char *prog) {
    print_banner();
    printf("%sUsage:%s %s <command> [subcommand] [arguments]\n\n", COLOR_BOLD, COLOR_RESET, prog);

    printf("%sInteractive Lock Client & Server:%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("  %s server                           Start centralized lock manager\n", prog);
    printf("  %s client shell <CLIENT_ID>         Interactive live lock shell (REPL)\n", prog);
    printf("  %s client lock <CLIENT_ID>          Acquire exclusive lock\n", prog);
    printf("  %s client unlock <CLIENT_ID>        Release lock\n", prog);
    printf("  %s client write <CLIENT_ID> <DATA>  Safely write to protected resource\n", prog);
    printf("  %s client read <CLIENT_ID>          Read from protected resource\n", prog);
    printf("  %s client status <CLIENT_ID>        Query server queue and active owner\n", prog);
    printf("  %s client ping <CLIENT_ID>          Check server latency/health\n", prog);
    printf("  %s client shutdown <CLIENT_ID>      Gracefully terminate lock server\n\n", prog);

    printf("%sInteractive Demonstrations (Prompts for runtime inputs):%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("  %s scheduler                        Interactive CPU Scheduler (FCFS, RR, Priority)\n", prog);
    printf("  %s monitor [PID]                    Live /proc process inspection\n", prog);
    printf("  %s co4 pagefault [pages]            Interactive page allocation & fault counters\n", prog);
    printf("  %s co5 filesystem [path]            Live inode & metadata inspection of any file\n", prog);
    printf("  %s co6 race [threads] [iterations]  Live race condition with custom load\n", prog);
    printf("  %s co6 mutex [threads] [iterations] Multi-threaded synchronized counter\n", prog);
    printf("  %s co6 semaphore [capacity] [count] Counting semaphore resource pool\n", prog);
    printf("  %s co6 deadlock                     Coffman circular wait detection\n\n", prog);

    printf("%sCore OS Demonstrations:%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("  %s co1                              Syscall boundary & command execution\n", prog);
    printf("  %s co2 process                      Process lifecycle, fork, exec, zombie cleanup\n", prog);
    printf("  %s co2 groups                       Process group IDs, sessions, job control\n", prog);
    printf("  %s co3 pipe                         Anonymous pipe IPC (custom text)\n", prog);
    printf("  %s co3 fifo                         Named pipe (FIFO) communication\n", prog);
    printf("  %s co3 signal                       POSIX signal notifications\n", prog);
    printf("  %s co4 memory                       Virtual address space (/proc/self/maps)\n", prog);
    printf("  %s co4 cow                          Copy-on-Write memory decoupling\n", prog);
    printf("  %s co5 buffered                     Buffered stdio vs unbuffered syscalls\n", prog);
    printf("  %s co5 mmapfile                     Memory-mapped file I/O (mmap/msync)\n\n", prog);
}

int run_server(void);
int run_client(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "server") == 0) {
        return run_server();
    }

    if (strcmp(argv[1], "client") == 0) {
        return run_client(argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "co1") == 0) {
        return demo_co1_journey();
    }

    if (strcmp(argv[1], "co2") == 0) {
        if (argc < 3 || strcmp(argv[2], "process") == 0) return demo_co2_process();
        if (strcmp(argv[2], "groups") == 0) return demo_co2_groups();
        fprintf(stderr, "%sUnknown CO-2 command: %s%s\n", COLOR_RED, argv[2], COLOR_RESET);
        return 1;
    }

    if (strcmp(argv[1], "co3") == 0) {
        if (argc < 3 || strcmp(argv[2], "pipe") == 0) return demo_co3_pipe();
        if (strcmp(argv[2], "fifo") == 0) return demo_co3_fifo();
        if (strcmp(argv[2], "signal") == 0) return demo_co3_signal();
        fprintf(stderr, "%sUnknown CO-3 command: %s%s\n", COLOR_RED, argv[2], COLOR_RESET);
        return 1;
    }

    if (strcmp(argv[1], "co4") == 0) {
        if (argc < 3 || strcmp(argv[2], "memory") == 0) return demo_co4_memory();
        if (strcmp(argv[2], "cow") == 0) return demo_co4_cow();
        if (strcmp(argv[2], "pagefault") == 0) return demo_co4_pagefault();
        if (strcmp(argv[2], "errors") == 0) return demo_co4_errors();
        fprintf(stderr, "%sUnknown CO-4 command: %s%s\n", COLOR_RED, argv[2], COLOR_RESET);
        return 1;
    }

    if (strcmp(argv[1], "co5") == 0) {
        if (argc < 3 || strcmp(argv[2], "filesystem") == 0) return demo_co5_filesystem();
        if (strcmp(argv[2], "buffered") == 0) return demo_co5_buffered();
        if (strcmp(argv[2], "mmapfile") == 0) return demo_co5_mmapfile();
        fprintf(stderr, "%sUnknown CO-5 command: %s%s\n", COLOR_RED, argv[2], COLOR_RESET);
        return 1;
    }

    if (strcmp(argv[1], "co6") == 0) {
        if (argc < 3 || strcmp(argv[2], "race") == 0) return demo_co6_race();
        if (strcmp(argv[2], "mutex") == 0) return demo_co6_mutex();
        if (strcmp(argv[2], "condition") == 0) return demo_co6_condition();
        if (strcmp(argv[2], "semaphore") == 0) return demo_co6_semaphore();
        if (strcmp(argv[2], "deadlock") == 0) return demo_co6_deadlock();
        fprintf(stderr, "%sUnknown CO-6 command: %s%s\n", COLOR_RED, argv[2], COLOR_RESET);
        return 1;
    }

    if (strcmp(argv[1], "scheduler") == 0) {
        return demo_scheduler();
    }

    if (strcmp(argv[1], "monitor") == 0) {
        return demo_monitor();
    }

    fprintf(stderr, "%sUnknown command: %s%s\n", COLOR_RED, argv[1], COLOR_RESET);
    print_usage(argv[0]);
    return 1;
}
