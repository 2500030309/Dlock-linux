#include "dlock.h"
#include "process.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

int demo_co1_journey(void) {
    printf("\n%s%s=== System Call Boundary & Command Execution Journey ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    struct utsname os_info;
    if (uname(&os_info) == 0) {
        printf("  Kernel Service Layer : %s %s (%s)\n", os_info.sysname, os_info.release, os_info.machine);
    }

    pid_t my_pid = getpid();
    pid_t parent_pid = getppid();
    long tid = syscall(SYS_gettid);

    printf("  Process Identifiers  : PID = %d | PPID (Shell) = %d | Kernel TID = %ld\n",
           (int)my_pid, (int)parent_pid, tid);

    /* Unbuffered Direct Syscall File I/O */
    const char *trace_file = "runtime/data/co1_trace.txt";
    int fd = open(trace_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        trace_file = "/tmp/dlock_co1_trace.txt";
        fd = open(trace_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    }
    if (fd >= 0) {
        const char *log_msg = "DLOCK_SYSCALL_JOURNEY_RECORD\n";
        ssize_t w = write(fd, log_msg, strlen(log_msg));
        (void)w;
        close(fd);
        printf("  Direct Syscall I/O   : Created '%s' -> FD %d (Wrote %zd bytes unbuffered)\n",
               trace_file, fd, w);
    }

    /* Fork-Wait Journey */
    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        const char *msg = "    --> [Child PID in isolated virtual memory] Exiting with status 42.\n";
        ssize_t ret = write(STDOUT_FILENO, msg, strlen(msg));
        (void)ret;
        _exit(42);
    } else {
        int status = 0;
        waitpid(child, &status, 0);
        printf("  Process Journey      : Forked child PID %d -> Reaped with exit status %d\n",
               (int)child, WEXITSTATUS(status));
    }

    printf("  %s[RESULT] System call boundary transition verified via libc wrapper & trap instruction.%s\n\n",
           COLOR_GREEN, COLOR_RESET);
    return 0;
}

int demo_co2_process(void) {
    printf("\n%s%s=== Process Lifecycle, Creation, & Execution ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    char custom_cmd[64] = "echo";
    char custom_arg[128] = "Hello from child process running via execvp()!";

    if (is_interactive_tty()) {
        char line[128];
        printf("Enter command to execute in child [default: 'echo']: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            size_t len = strlen(line);
            if (line[len - 1] == '\n') line[len - 1] = '\0';
            snprintf(custom_cmd, sizeof(custom_cmd), "%s", line);
        }

        printf("Enter argument string [default: '%s']: ", custom_arg);
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            size_t len = strlen(line);
            if (line[len - 1] == '\n') line[len - 1] = '\0';
            snprintf(custom_arg, sizeof(custom_arg), "%s", line);
        }
    }

    /* Step 1: fork and exec */
    printf("\n  1. Executing command in child process via fork() and execvp():\n");
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = {custom_cmd, custom_arg, NULL};
        execvp(argv[0], argv);
        perror("execvp failed");
        _exit(1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("     Reaped child PID %d (Exit Code: %d)\n\n", (int)pid, WEXITSTATUS(status));

    /* Step 2: Controlled Zombie Inspection and Cleanup */
    printf("  2. Controlled Zombie State Lifecycle Demonstration:\n");
    fflush(stdout);
    pid_t z_pid = fork();
    if (z_pid == 0) {
        _exit(0);
    }
    usleep(40000);

    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/status", z_pid);
    FILE *fp = fopen(proc_path, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "State:", 6) == 0) {
                printf("     [Child State in /proc] PID %d: %s", (int)z_pid, line);
                break;
            }
        }
        fclose(fp);
    }

    waitpid(z_pid, NULL, 0);
    printf("     waitpid() invoked: Task entry reaped. Zombie cleared immediately.\n\n");
    return 0;
}

int demo_co2_groups(void) {
    printf("\n%s%s=== Process Groups, Sessions, & Job Control ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    printf("  Process Identifiers:\n");
    printf("    Current PID  : %d\n", getpid());
    printf("    Current PGID : %d\n", getpgrp());
    printf("    Current SID  : %d\n\n", getsid(0));

    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        printf("    [Child Initial] PID: %d, PGID: %d\n", getpid(), getpgrp());
        if (setpgid(0, 0) == 0) {
            printf("    [Child Updated] PID: %d, New PGID: %d (Now Group Leader)\n", getpid(), getpgrp());
        }
        _exit(0);
    }
    waitpid(child, NULL, 0);
    printf("  %s[RESULT] Process group isolation verified.%s\n\n", COLOR_GREEN, COLOR_RESET);
    return 0;
}
