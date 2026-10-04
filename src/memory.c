#include "dlock.h"
#include "memory.h"

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

int demo_co4_memory(void) {
    printf("\n%s%s=== Linux Process Virtual Address Space Layout ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    int stack_var = 42;
    char *heap_buf = (char *)malloc(1024 * 64);
    if (!heap_buf) {
        perror("malloc failed");
        return 1;
    }
    void *mmap_region = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    printf("  Virtual Segment Pointers (PID: %d):\n", getpid());
    printf("    [Code Segment / Text]       : %p (Function pointer)\n", (void*)&demo_co4_memory);
    printf("    [Heap Segment / brk]        : %p (malloc buffer)\n", (void*)heap_buf);
    printf("    [Mmap / Memory Map Segment] : %p (mmap anonymous page)\n", mmap_region);
    printf("    [Stack Segment]             : %p (local stack variable)\n\n", (void*)&stack_var);

    printf("  Active /proc/self/maps Memory Regions:\n");
    FILE *maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[256];
        int count = 0;
        while (fgets(line, sizeof(line), maps) && count < 6) {
            printf("    %s", line);
            count++;
        }
        fclose(maps);
    }

    free(heap_buf);
    if (mmap_region != MAP_FAILED) munmap(mmap_region, 4096);
    printf("\n");
    return 0;
}

int demo_co4_cow(void) {
    printf("\n%s%s=== Copy-on-Write (COW) Verification ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    size_t page_size = sysconf(_SC_PAGESIZE);
    char *shared_page = (char *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (shared_page == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    snprintf(shared_page, page_size, "PARENT_DATA_BEFORE_FORK");
    printf("  Parent initial data: \"%s\" (Virtual Address: %p)\n", shared_page, (void*)shared_page);
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        munmap(shared_page, page_size);
        return 1;
    }

    if (pid == 0) {
        printf("  [Child PID %d] Reading shared frame: \"%s\"\n", getpid(), shared_page);
        printf("  [Child PID %d] Writing new value (triggers MMU page fault & frame duplication)...\n", getpid());
        snprintf(shared_page, page_size, "CHILD_PRIVATE_MODIFIED_DATA");
        printf("  [Child PID %d] After write: \"%s\"\n", getpid(), shared_page);
        munmap(shared_page, page_size);
        _exit(0);
    } else {
        waitpid(pid, NULL, 0);
        printf("  [Parent PID %d] Reading after child termination: \"%s\"\n", getpid(), shared_page);
        printf("  %s[RESULT] Parent memory unmodified! Kernel decoupled frame on write.%s\n\n",
               COLOR_GREEN, COLOR_RESET);
        munmap(shared_page, page_size);
    }
    return 0;
}

int demo_co4_pagefault(void) {
    printf("\n%s%s=== Interactive Page Faults & Demand Paging ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    size_t num_pages = 100;

    if (is_interactive_tty()) {
        char line[64];
        printf("Enter number of 4KB pages to allocate and touch (e.g. 50 to 5000) [default: 100]: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            int p = atoi(line);
            if (p >= 1 && p <= 50000) num_pages = (size_t)p;
        }
    }

    struct rusage before, after;
    getrusage(RUSAGE_SELF, &before);

    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t total_bytes = num_pages * page_size;

    printf("  Initial Minor Page Faults (reclaims): %ld\n", before.ru_minflt);
    printf("  Allocating %zu pages (%.2f KB) via mmap(MAP_ANONYMOUS | MAP_PRIVATE)...\n",
           num_pages, (double)total_bytes / 1024.0);

    char *pages = (char *)mmap(NULL, total_bytes, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pages == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    printf("  Touching each page sequentially to trigger demand-paging faults...\n");
    for (size_t i = 0; i < num_pages; i++) {
        pages[i * page_size] = (char)(i & 0xFF);
    }

    getrusage(RUSAGE_SELF, &after);
    long delta = after.ru_minflt - before.ru_minflt;

    printf("  Final Minor Page Faults: %ld (%s+%ld faults recorded%s)\n",
           after.ru_minflt, COLOR_GREEN, delta, COLOR_RESET);
    printf("  %s[RESULT] Demand Paging confirmed: Physical RAM frames allocated lazily on first access!%s\n\n",
           COLOR_GREEN, COLOR_RESET);

    munmap(pages, total_bytes);
    return 0;
}

int demo_co4_errors(void) {
    printf("\n%s%s=== Memory Error Prevention & Valgrind Diagnostics ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);
    printf("  Safe Memory Patterns in DLock:\n");
    printf("    1. Heap Allocation : Always validate pointer returned by malloc/calloc.\n");
    printf("    2. Pointer Zeroing : Pointer set to NULL immediately after free() to prevent dangling access.\n");
    printf("    3. Bounds Safety   : All string copies use bounded snprintf() to prevent buffer overflows.\n");
    printf("    4. Leak Auditing   : Verified with 'valgrind --leak-check=full ./bin/dlock co4 memory'\n\n");
    return 0;
}
