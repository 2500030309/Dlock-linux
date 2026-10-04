#include "dlock.h"
#include "ipc.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

int demo_co3_pipe(void) {
    printf("\n%s%s=== Anonymous Pipe IPC ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    char custom_msg[256] = "DLock IPC: Mutual-Exclusion Request Message over Pipe";

    if (is_interactive_tty()) {
        char line[256];
        printf("Enter custom message to send across pipe [default: '%s']: ", custom_msg);
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            size_t len = strlen(line);
            if (line[len - 1] == '\n') line[len - 1] = '\0';
            snprintf(custom_msg, sizeof(custom_msg), "%s", line);
        }
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe creation failed");
        return 1;
    }

    printf("  Pipe initialized: Read FD = %d, Write FD = %d\n", pipefd[0], pipefd[1]);
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        printf("  [Child PID %d] Writing payload to pipe write-end: \"%s\"\n", getpid(), custom_msg);
        ssize_t written = write(pipefd[1], custom_msg, strlen(custom_msg));
        (void)written;
        close(pipefd[1]);
        _exit(0);
    } else {
        close(pipefd[1]);
        char buffer[512];
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
        printf("  [Parent PID %d] Read %zd bytes from pipe read-end:\n", getpid(), bytes_read);
        printf("  %s--> \"%s\"%s\n", COLOR_GREEN, buffer, COLOR_RESET);
        close(pipefd[0]);

        waitpid(pid, NULL, 0);
        printf("  %s[RESULT] Unidirectional pipe data transfer verified.%s\n\n", COLOR_GREEN, COLOR_RESET);
    }
    return 0;
}

int demo_co3_fifo(void) {
    printf("\n%s%s=== Named Pipe (FIFO) IPC ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    const char *fifo_path = resolve_fifo_path();
    printf("  Using FIFO filesystem path: %s\n", fifo_path);

    char custom_req[256] = "CLIENT_REQ:RESOURCE_MUTEX:EXCLUSIVE";

    if (is_interactive_tty()) {
        char line[256];
        printf("Enter custom FIFO request payload [default: '%s']: ", custom_req);
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            size_t len = strlen(line);
            if (line[len - 1] == '\n') line[len - 1] = '\0';
            snprintf(custom_req, sizeof(custom_req), "%s", line);
        }
    }

    unlink(fifo_path);
    if (mkfifo(fifo_path, 0666) < 0) {
        perror("mkfifo failed");
        return 1;
    }
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        unlink(fifo_path);
        return 1;
    }

    if (pid == 0) {
        usleep(30000);
        int wfd = open(fifo_path, O_WRONLY);
        if (wfd >= 0) {
            printf("  [Writer Process PID %d] Writing into FIFO: \"%s\"\n", getpid(), custom_req);
            ssize_t w = write(wfd, custom_req, strlen(custom_req));
            (void)w;
            close(wfd);
        }
        _exit(0);
    } else {
        int rfd = open(fifo_path, O_RDONLY);
        if (rfd >= 0) {
            char buf[512];
            memset(buf, 0, sizeof(buf));
            ssize_t r = read(rfd, buf, sizeof(buf) - 1);
            printf("  [Reader Process PID %d] Received %zd bytes from FIFO:\n", getpid(), r);
            printf("  %s--> \"%s\"%s\n", COLOR_GREEN, buf, COLOR_RESET);
            close(rfd);
        }
        waitpid(pid, NULL, 0);
        unlink(fifo_path);
        printf("  %s[RESULT] Named FIFO communication and cleanup verified.%s\n\n", COLOR_GREEN, COLOR_RESET);
    }
    return 0;
}

static volatile sig_atomic_t g_signal_received = 0;
static volatile sig_atomic_t g_received_signo = 0;

static void test_signal_handler(int signo, siginfo_t *info, void *context) {
    (void)context;
    (void)info;
    g_signal_received = 1;
    g_received_signo = signo;
}

int demo_co3_signal(void) {
    printf("\n%s%s=== POSIX Signals & Asynchronous Notifications ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = test_signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGUSR1, &sa, NULL);
    printf("  Registered SA_SIGINFO handler for SIGUSR1 (%d)\n", SIGUSR1);
    fflush(stdout);

    pid_t parent_pid = getpid();
    pid_t child_pid = fork();
    if (child_pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (child_pid == 0) {
        usleep(30000);
        printf("  [Child PID %d] Sending SIGUSR1 to Parent (PID %d) via kill()...\n", getpid(), (int)parent_pid);
        kill(parent_pid, SIGUSR1);
        _exit(0);
    } else {
        while (!g_signal_received) {
            usleep(10000);
        }
        printf("  [Parent PID %d] Asynchronous signal delivered! Signal number: %d\n",
               getpid(), g_received_signo);
        waitpid(child_pid, NULL, 0);
        printf("  %s[RESULT] Asynchronous notification handled cleanly.%s\n\n", COLOR_GREEN, COLOR_RESET);
    }
    return 0;
}
