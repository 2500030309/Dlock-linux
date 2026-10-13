/**
 * @file thread_pool.h
 * @brief Fixed-size POSIX threads worker pool with bounded job queue.
 *
 * Implements a producer-consumer thread pool where the main accept loop pushes
 * newly accepted client socket file descriptors, and worker threads pop them to
 * run individual client sessions. Synchronization uses a mutex and two condition
 * variables (not_empty and not_full).
 */

#ifndef DLOCK_THREAD_POOL_H
#define DLOCK_THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>

typedef void (*job_handler_t)(int client_fd);

typedef struct {
    int *queue;
    int capacity;
    int head;
    int tail;
    int count;

    pthread_t *threads;
    int num_workers;
    job_handler_t handler;

    bool shutdown;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} thread_pool_t;

/**
 * @brief Create and start a worker thread pool.
 *
 * @param num_workers Number of worker threads to spawn.
 * @param queue_capacity Maximum backlog of queued socket descriptors.
 * @param handler Function pointer invoked by workers for each client descriptor.
 * @return thread_pool_t* Allocated pool instance, or NULL on error.
 */
thread_pool_t *thread_pool_create(int num_workers, int queue_capacity, job_handler_t handler);

/**
 * @brief Enqueue a client file descriptor for processing.
 *
 * Blocks if the job queue is full until space becomes available or shutdown occurs.
 *
 * @param pool Target thread pool.
 * @param client_fd Connected client socket descriptor.
 * @return int 0 on success, -1 if pool is shutting down.
 */
int thread_pool_enqueue(thread_pool_t *pool, int client_fd);

/**
 * @brief Signal worker threads to finish and terminate the pool.
 *
 * @param pool Target thread pool.
 */
void thread_pool_stop(thread_pool_t *pool);

/**
 * @brief Free resources associated with the pool.
 *
 * @param pool Target thread pool.
 */
void thread_pool_destroy(thread_pool_t *pool);

#endif /* DLOCK_THREAD_POOL_H */
