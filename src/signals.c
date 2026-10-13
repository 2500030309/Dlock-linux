/**
 * @file signals.c
 * @brief Async-signal-safe signal handling using the self-pipe technique.
 *
 * Configures signal dispositions via sigaction. Handlers are strictly
 * async-signal-safe and do not allocate memory, invoke printf, or acquire mutexes;
 * instead, they write a single byte into an anonymous pipe to wake the main
 * poll() loop.
 */

#include "signals.h"
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static int s_pipe_fds[2] = {-1, -1};
static volatile sig_atomic_t s_dump_requested = 0;

/* Async-signal-safe signal handler */
static void signal_handler(int sig) {
    int saved_errno = errno;
    char byte = 0;

    if (sig == SIGINT || sig == SIGTERM) {
        byte = 'S'; /* Shutdown byte */
        (void)write(s_pipe_fds[1], &byte, 1);
    } else if (sig == SIGUSR1) {
        s_dump_requested = 1;
        byte = 'D'; /* Dump diagnostic byte */
        (void)write(s_pipe_fds[1], &byte, 1);
    }

    errno = saved_errno;
}

int signals_init(void) {
    if (pipe(s_pipe_fds) < 0) {
        return -1;
    }

    /* Set pipe fds to non-blocking */
    int flags = fcntl(s_pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(s_pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    }
    flags = fcntl(s_pipe_fds[1], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(s_pipe_fds[1], F_SETFL, flags | O_NONBLOCK);
    }

    /* Configure SIGINT & SIGTERM */
    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = signal_handler;
    sigfillset(&sa_term.sa_mask);
    sa_term.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa_term, NULL) < 0) return -1;
    if (sigaction(SIGTERM, &sa_term, NULL) < 0) return -1;

    /* Configure SIGUSR1 */
    struct sigaction sa_usr;
    memset(&sa_usr, 0, sizeof(sa_usr));
    sa_usr.sa_handler = signal_handler;
    sigfillset(&sa_usr.sa_mask);
    sa_usr.sa_flags = SA_RESTART;

    if (sigaction(SIGUSR1, &sa_usr, NULL) < 0) return -1;

    /* Ignore SIGPIPE to prevent crash on writing to disconnected socket */
    struct sigaction sa_pipe;
    memset(&sa_pipe, 0, sizeof(sa_pipe));
    sa_pipe.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) < 0) return -1;

    return 0;
}

int signals_get_pipe_read_fd(void) {
    return s_pipe_fds[0];
}

bool signals_check_dump_requested(void) {
    if (s_dump_requested) {
        s_dump_requested = 0;
        return true;
    }
    return false;
}

void signals_trigger_shutdown(void) {
    if (s_pipe_fds[1] >= 0) {
        char byte = 'S';
        (void)write(s_pipe_fds[1], &byte, 1);
    }
}

void signals_cleanup(void) {
    if (s_pipe_fds[0] >= 0) {
        close(s_pipe_fds[0]);
        s_pipe_fds[0] = -1;
    }
    if (s_pipe_fds[1] >= 0) {
        close(s_pipe_fds[1]);
        s_pipe_fds[1] = -1;
    }
}
