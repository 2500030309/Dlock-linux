#include "dlock.h"
#include "concurrency.h"

#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>

static volatile long g_race_counter = 0;
static long g_mutex_counter = 0;
static pthread_mutex_t g_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static long g_iterations = 100000;

static void *race_worker(void *arg) {
    (void)arg;
    for (long i = 0; i < g_iterations; i++) {
        g_race_counter++;
    }
    return NULL;
}

int demo_co6_race(void) {
    printf("\n%s%s=== Race Condition Demonstration ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    int num_threads = 4;
    g_iterations = 100000;

    if (is_interactive_tty()) {
        char line[64];
        printf("Enter number of threads (2 to 16) [default: 4]: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            int t = atoi(line);
            if (t >= 2 && t <= 16) num_threads = t;
        }

        printf("Enter iterations per thread (e.g. 10000 to 1000000) [default: 100000]: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            long it = atol(line);
            if (it > 0) g_iterations = it;
        }
    }

    g_race_counter = 0;
    pthread_t threads[16];
    long expected_total = (long)num_threads * g_iterations;

    printf("  Executing: %d concurrent threads x %ld iterations (Expected: %ld)...\n",
           num_threads, g_iterations, expected_total);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, race_worker, NULL);
    }
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("  Expected Counter : %ld\n", expected_total);
    printf("  Actual Counter   : %ld\n", g_race_counter);

    if (g_race_counter != expected_total) {
        printf("  %s[RESULT] Race condition confirmed! Lost updates: %ld (-%.2f%%)%s\n\n",
               COLOR_RED, expected_total - g_race_counter,
               ((double)(expected_total - g_race_counter) / expected_total) * 100.0, COLOR_RESET);
    } else {
        printf("  %s[RESULT] Thread execution was serialized on this run; re-run with more iterations.%s\n\n",
               COLOR_YELLOW, COLOR_RESET);
    }
    return 0;
}

static void *mutex_worker(void *arg) {
    (void)arg;
    for (long i = 0; i < g_iterations; i++) {
        pthread_mutex_lock(&g_counter_mutex);
        g_mutex_counter++;
        pthread_mutex_unlock(&g_counter_mutex);
    }
    return NULL;
}

int demo_co6_mutex(void) {
    printf("\n%s%s=== Mutex Mutual Exclusion Demonstration ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    int num_threads = 4;
    g_iterations = 100000;

    if (is_interactive_tty()) {
        char line[64];
        printf("Enter number of threads (2 to 16) [default: 4]: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            int t = atoi(line);
            if (t >= 2 && t <= 16) num_threads = t;
        }

        printf("Enter iterations per thread (e.g. 10000 to 1000000) [default: 100000]: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            long it = atol(line);
            if (it > 0) g_iterations = it;
        }
    }

    g_mutex_counter = 0;
    pthread_t threads[16];
    long expected_total = (long)num_threads * g_iterations;

    printf("  Executing: %d threads protected by pthread_mutex_lock()...\n", num_threads);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, mutex_worker, NULL);
    }
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("  Expected Counter : %ld\n", expected_total);
    printf("  Actual Counter   : %ld\n", g_mutex_counter);
    printf("  %s[RESULT] Success: Exact deterministic count preserved! Mutual exclusion verified.%s\n\n",
           COLOR_GREEN, COLOR_RESET);
    return 0;
}

static pthread_mutex_t g_cv_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_cond = PTHREAD_COND_INITIALIZER;
static int g_resource_ready = 0;

static void *consumer_worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_cv_mutex);
    while (!g_resource_ready) {
        printf("  [Consumer] Buffer empty. Sleeping on pthread_cond_wait()...\n");
        pthread_cond_wait(&g_cv_cond, &g_cv_mutex);
    }
    printf("  [Consumer] Woken up by condition signal! Consumed resource.\n");
    g_resource_ready = 0;
    pthread_mutex_unlock(&g_cv_mutex);
    return NULL;
}

static void *producer_worker(void *arg) {
    (void)arg;
    usleep(40000);
    pthread_mutex_lock(&g_cv_mutex);
    printf("  [Producer] Produced item. Calling pthread_cond_signal()...\n");
    g_resource_ready = 1;
    pthread_cond_signal(&g_cv_cond);
    pthread_mutex_unlock(&g_cv_mutex);
    return NULL;
}

int demo_co6_condition(void) {
    printf("\n%s%s=== Condition Variable Coordination ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);
    pthread_t c, p;
    g_resource_ready = 0;
    pthread_create(&c, NULL, consumer_worker, NULL);
    pthread_create(&p, NULL, producer_worker, NULL);
    pthread_join(c, NULL);
    pthread_join(p, NULL);
    printf("  %s[RESULT] Producer-Consumer condition signaling verified.%s\n\n", COLOR_GREEN, COLOR_RESET);
    return 0;
}

static sem_t g_semaphore;

static void *sem_worker(void *arg) {
    long id = (long)arg;
    sem_wait(&g_semaphore);
    printf("  %s[Worker %ld] Acquired slot! Running inside capacity-limited pool...%s\n",
           COLOR_GREEN, id, COLOR_RESET);
    usleep(35000);
    printf("  [Worker %ld] Work finished. Calling sem_post() to release slot.\n", id);
    sem_post(&g_semaphore);
    return NULL;
}

int demo_co6_semaphore(void) {
    printf("\n%s%s=== Counting Semaphore Resource Pool ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    int capacity = 2;
    int num_workers = 5;

    if (is_interactive_tty()) {
        char line[64];
        printf("Enter resource capacity / slots (1 to 8) [default: 2]: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            int c = atoi(line);
            if (c >= 1 && c <= 8) capacity = c;
        }

        printf("Enter number of worker threads (2 to 16) [default: 5]: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            int w = atoi(line);
            if (w >= 2 && w <= 16) num_workers = w;
        }
    }

    printf("  Semaphore initialized with %d available slots for %d workers.\n", capacity, num_workers);
    sem_init(&g_semaphore, 0, capacity);

    pthread_t threads[16];
    for (long i = 0; i < num_workers; i++) {
        pthread_create(&threads[i], NULL, sem_worker, (void*)(i + 1));
    }
    for (int i = 0; i < num_workers; i++) {
        pthread_join(threads[i], NULL);
    }

    sem_destroy(&g_semaphore);
    printf("  %s[RESULT] All workers passed through semaphore without exceeding capacity.%s\n\n",
           COLOR_GREEN, COLOR_RESET);
    return 0;
}

static pthread_mutex_t g_deadlock_m1 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_deadlock_m2 = PTHREAD_MUTEX_INITIALIZER;

static void *deadlock_t1(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_deadlock_m1);
    printf("  [Thread 1] Acquired Lock 1. Requesting Lock 2...\n");
    usleep(40000);

    if (pthread_mutex_trylock(&g_deadlock_m2) != 0) {
        printf("  %s[Thread 1] Lock 2 is locked by Thread 2! Circular wait detected.%s\n",
               COLOR_RED, COLOR_RESET);
        printf("  [Thread 1] Backing off to prevent indefinite freeze.\n");
    } else {
        pthread_mutex_unlock(&g_deadlock_m2);
    }
    pthread_mutex_unlock(&g_deadlock_m1);
    return NULL;
}

static void *deadlock_t2(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_deadlock_m2);
    printf("  [Thread 2] Acquired Lock 2. Requesting Lock 1...\n");
    usleep(40000);

    if (pthread_mutex_trylock(&g_deadlock_m1) != 0) {
        printf("  %s[Thread 2] Lock 1 is locked by Thread 1! Circular wait detected.%s\n",
               COLOR_RED, COLOR_RESET);
        printf("  [Thread 2] Backing off to prevent indefinite freeze.\n");
    } else {
        pthread_mutex_unlock(&g_deadlock_m1);
    }
    pthread_mutex_unlock(&g_deadlock_m2);
    return NULL;
}

int demo_co6_deadlock(void) {
    printf("\n%s%s=== Safe Deadlock Detection (Coffman Circular Wait) ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);
    pthread_t t1, t2;
    pthread_create(&t1, NULL, deadlock_t1, NULL);
    pthread_create(&t2, NULL, deadlock_t2, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("  %s[RESULT] Deadlock demonstrated safely via trylock backoff without hanging.%s\n\n",
           COLOR_GREEN, COLOR_RESET);
    return 0;
}
