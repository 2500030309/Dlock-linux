/**
 * @file thread_pool.c
 * @brief Fixed worker thread pool with bounded FIFO job queue.
 *
 * Implements a producer-consumer pattern using POSIX pthreads, a mutex,
 * and two condition variables (not_empty, not_full). Main accept loop
 * produces client socket descriptors; pool workers consume descriptors and
 * invoke the session handler until client disconnects.
 */

#include "thread_pool.h"
#include "logger.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static void *worker_thread(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;

    while (true) {
        int client_fd = -1;

        pthread_mutex_lock(&pool->lock);

        while (pool->count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->not_empty, &pool->lock);
        }

        if (pool->shutdown && pool->count == 0) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        /* Pop job from circular buffer */
        client_fd = pool->queue[pool->head];
        pool->head = (pool->head + 1) % pool->capacity;
        pool->count--;

        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->lock);

        /* Process client session */
        if (client_fd >= 0 && pool->handler != NULL) {
            pool->handler(client_fd);
        }
    }

    return NULL;
}

thread_pool_t *thread_pool_create(int num_workers, int queue_capacity, job_handler_t handler) {
    if (num_workers <= 0 || queue_capacity <= 0 || handler == NULL) {
        return NULL;
    }

    thread_pool_t *pool = (thread_pool_t *)calloc(1, sizeof(thread_pool_t));
    if (pool == NULL) {
        return NULL;
    }

    pool->capacity = queue_capacity;
    pool->num_workers = num_workers;
    pool->handler = handler;
    pool->head = 0;
    pool->tail = 0;
    pool->count = 0;
    pool->shutdown = false;

    pool->queue = (int *)calloc((size_t)queue_capacity, sizeof(int));
    pool->threads = (pthread_t *)calloc((size_t)num_workers, sizeof(pthread_t));
    if (pool->queue == NULL || pool->threads == NULL) {
        free(pool->queue);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->not_full, NULL);

    /* Spawn worker threads */
    for (int i = 0; i < num_workers; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            logger_log(LOG_LEVEL_ERROR, "Failed to spawn thread pool worker %d", i);
            thread_pool_stop(pool);
            thread_pool_destroy(pool);
            return NULL;
        }
    }

    logger_log(LOG_LEVEL_INFO, "Thread pool initialized with %d workers (queue capacity %d)",
               num_workers, queue_capacity);
    return pool;
}

int thread_pool_enqueue(thread_pool_t *pool, int client_fd) {
    if (pool == NULL || client_fd < 0) {
        return -1;
    }

    pthread_mutex_lock(&pool->lock);

    while (pool->count == pool->capacity && !pool->shutdown) {
        pthread_cond_wait(&pool->not_full, &pool->lock);
    }

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }

    pool->queue[pool->tail] = client_fd;
    pool->tail = (pool->tail + 1) % pool->capacity;
    pool->count++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

void thread_pool_stop(thread_pool_t *pool) {
    if (pool == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_cond_broadcast(&pool->not_full);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->num_workers; i++) {
        pthread_join(pool->threads[i], NULL);
    }
}

void thread_pool_destroy(thread_pool_t *pool) {
    if (pool == NULL) {
        return;
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);

    free(pool->queue);
    free(pool->threads);
    free(pool);
}
