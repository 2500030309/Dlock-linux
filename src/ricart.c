/**
 * @file ricart.c
 * @brief Peer-to-peer distributed mutual exclusion using Ricart–Agrawala algorithm.
 *
 * Implements the decentralized Ricart–Agrawala algorithm with Lamport logical clocks.
 * Peer nodes coordinate entry into critical sections without a centralized server.
 * Binary target: bin/dlockpeer.
 */

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_PEERS 32

typedef struct {
    char host[64];
    int port;
} peer_info_t;

typedef enum {
    MSG_REQUEST = 1,
    MSG_REPLY   = 2
} ricart_msg_type_t;

typedef struct {
    ricart_msg_type_t type;
    int node_id;
    uint64_t timestamp;
} ricart_packet_t;

static int s_my_id = 0;
static int s_my_port = 0;
static peer_info_t s_peers[MAX_PEERS];
static int s_peer_count = 0;

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_reply_cond = PTHREAD_COND_INITIALIZER;
static uint64_t s_lamport_clock = 0;
static uint64_t s_request_ts = 0;
static bool s_requesting = false;
static int s_replies_received = 0;
static bool s_deferred[MAX_PEERS] = {false};
static bool s_running = true;

static uint64_t max_u64(uint64_t a, uint64_t b) {
    return (a > b) ? a : b;
}

static void send_to_peer(int peer_idx, const ricart_packet_t *pkt) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)s_peers[peer_idx].port);
    inet_pton(AF_INET, s_peers[peer_idx].host, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        char wire[64];
        snprintf(wire, sizeof(wire), "%d %d %llu\n", pkt->type, pkt->node_id, (unsigned long long)pkt->timestamp);
        (void)write(fd, wire, strlen(wire));
    }
    close(fd);
}

static void *listener_thread(void *arg) {
    (void)arg;
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) return NULL;

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)s_my_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(listen_fd, 32) < 0) {
        close(listen_fd);
        return NULL;
    }

    while (s_running) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        char buf[64];
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            int type = 0, sender_id = 0;
            unsigned long long ts = 0;
            if (sscanf(buf, "%d %d %llu", &type, &sender_id, &ts) == 3) {
                pthread_mutex_lock(&s_mutex);
                s_lamport_clock = max_u64(s_lamport_clock, (uint64_t)ts) + 1;

                if (type == MSG_REQUEST) {
                    bool defer = false;
                    if (s_requesting) {
                        /* Prioritize lower timestamp; break ties with lower node_id */
                        if (s_request_ts < ts || (s_request_ts == ts && s_my_id < sender_id)) {
                            defer = true;
                        }
                    }

                    if (defer) {
                        s_deferred[sender_id] = true;
                    } else {
                        ricart_packet_t reply = {
                            .type = MSG_REPLY,
                            .node_id = s_my_id,
                            .timestamp = s_lamport_clock
                        };
                        send_to_peer(sender_id, &reply);
                    }
                } else if (type == MSG_REPLY) {
                    s_replies_received++;
                    if (s_replies_received >= s_peer_count - 1) {
                        pthread_cond_signal(&s_reply_cond);
                    }
                }
                pthread_mutex_unlock(&s_mutex);
            }
        }
        close(client_fd);
    }

    close(listen_fd);
    return NULL;
}

void ricart_acquire(void) {
    pthread_mutex_lock(&s_mutex);
    s_requesting = true;
    s_lamport_clock++;
    s_request_ts = s_lamport_clock;
    s_replies_received = 0;

    ricart_packet_t req = {
        .type = MSG_REQUEST,
        .node_id = s_my_id,
        .timestamp = s_request_ts
    };

    pthread_mutex_unlock(&s_mutex);

    /* Broadcast REQUEST to all other peers */
    for (int i = 0; i < s_peer_count; i++) {
        if (i != s_my_id) {
            send_to_peer(i, &req);
        }
    }

    pthread_mutex_lock(&s_mutex);
    while (s_replies_received < s_peer_count - 1) {
        pthread_cond_wait(&s_reply_cond, &s_mutex);
    }
    pthread_mutex_unlock(&s_mutex);
}

void ricart_release(void) {
    pthread_mutex_lock(&s_mutex);
    s_requesting = false;

    /* Flush deferred replies */
    for (int i = 0; i < s_peer_count; i++) {
        if (s_deferred[i]) {
            s_deferred[i] = false;
            ricart_packet_t reply = {
                .type = MSG_REPLY,
                .node_id = s_my_id,
                .timestamp = s_lamport_clock
            };
            send_to_peer(i, &reply);
        }
    }
    pthread_mutex_unlock(&s_mutex);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <node_id> <host1:port1> [host2:port2 ...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    s_my_id = atoi(argv[1]);
    s_peer_count = argc - 2;

    for (int i = 0; i < s_peer_count; i++) {
        char *spec = argv[i + 2];
        char *colon = strchr(spec, ':');
        if (colon == NULL) {
            fprintf(stderr, "Invalid peer format: %s\n", spec);
            return EXIT_FAILURE;
        }
        *colon = '\0';
        strncpy(s_peers[i].host, spec, sizeof(s_peers[i].host) - 1);
        s_peers[i].port = atoi(colon + 1);
    }

    s_my_port = s_peers[s_my_id].port;
    printf("[Peer %d] Starting Ricart-Agrawala peer on port %d\n", s_my_id, s_my_port);

    pthread_t listener;
    pthread_create(&listener, NULL, listener_thread, NULL);

    sleep(1); /* Allow peers to initialize */

    printf("[Peer %d] Requesting Critical Section...\n", s_my_id);
    ricart_acquire();
    printf("[Peer %d] === ENTERED CRITICAL SECTION ===\n", s_my_id);
    sleep(2);
    printf("[Peer %d] === LEAVING CRITICAL SECTION ===\n", s_my_id);
    ricart_release();

    s_running = false;
    pthread_join(listener, NULL);
    return EXIT_SUCCESS;
}
