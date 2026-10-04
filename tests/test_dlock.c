#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int total_tests = 0;
static int passed_tests = 0;

static void run_test(const char *test_name, const char *command, int expected_exit) {
    total_tests++;
    printf("Test %-2d: %-46s ... ", total_tests, test_name);
    fflush(stdout);

    int ret = system(command);
    int exit_code = WEXITSTATUS(ret);

    if (exit_code == expected_exit) {
        printf("\033[32m[PASS]\033[0m\n");
        passed_tests++;
    } else {
        printf("\033[31m[FAIL]\033[0m (expected %d, got %d)\n", expected_exit, exit_code);
    }
}

int main(void) {
    printf("\n========================================================\n");
    printf("  DLOCK FULL SYLLABUS AUTOMATED VERIFICATION SUITE       \n");
    printf("========================================================\n\n");

    /* Base CLI Tests */
    run_test("CLI Help Flag (-h)", "./bin/dlock -h > /dev/null 2>&1", 0);
    run_test("CLI Help Command (help)", "./bin/dlock help > /dev/null 2>&1", 0);
    run_test("Invalid Command Rejection", "./bin/dlock invalid_cmd > /dev/null 2>&1", 1);

    /* CO-1 Tests */
    run_test("CO-1 Syscall & Command Journey", "./bin/dlock co1 > /dev/null 2>&1", 0);
    struct stat st;
    total_tests++;
    printf("Test %-2d: %-46s ... ", total_tests, "CO-1 Unbuffered Trace File Generation");
    if ((stat("runtime/data/co1_trace.txt", &st) == 0 && st.st_size > 0) ||
        (stat("/tmp/dlock_co1_trace.txt", &st) == 0 && st.st_size > 0)) {
        printf("\033[32m[PASS]\033[0m\n");
        passed_tests++;
    } else {
        printf("\033[31m[FAIL]\033[0m\n");
    }

    /* CO-2 Tests */
    run_test("CO-2 Process Lifecycle & fork/exec/wait", "./bin/dlock co2 process > /dev/null 2>&1", 0);
    run_test("CO-2 Process Groups & Sessions", "./bin/dlock co2 groups > /dev/null 2>&1", 0);

    /* CO-3 Tests */
    run_test("CO-3 Anonymous Pipe IPC", "./bin/dlock co3 pipe > /dev/null 2>&1", 0);
    run_test("CO-3 Named Pipe (FIFO) IPC", "./bin/dlock co3 fifo > /dev/null 2>&1", 0);
    run_test("CO-3 POSIX Signals & sigaction", "./bin/dlock co3 signal > /dev/null 2>&1", 0);

    /* CO-4 Tests */
    run_test("CO-4 Virtual Memory & Address Space", "./bin/dlock co4 memory > /dev/null 2>&1", 0);
    run_test("CO-4 Copy-On-Write (COW)", "./bin/dlock co4 cow > /dev/null 2>&1", 0);
    run_test("CO-4 Page Faults & Demand Paging", "./bin/dlock co4 pagefault > /dev/null 2>&1", 0);
    run_test("CO-4 Memory Error Safety & Valgrind Demo", "./bin/dlock co4 errors > /dev/null 2>&1", 0);

    /* CO-5 Tests */
    run_test("CO-5 Filesystem Inode & Metadata", "./bin/dlock co5 filesystem > /dev/null 2>&1", 0);
    run_test("CO-5 Buffered vs Unbuffered I/O", "./bin/dlock co5 buffered > /dev/null 2>&1", 0);
    run_test("CO-5 Memory-Mapped File I/O (mmap)", "./bin/dlock co5 mmapfile > /dev/null 2>&1", 0);

    /* CO-6 Tests */
    run_test("CO-6 Race Condition Demonstration", "./bin/dlock co6 race > /dev/null 2>&1", 0);
    run_test("CO-6 Mutex Mutual Exclusion", "./bin/dlock co6 mutex > /dev/null 2>&1", 0);
    run_test("CO-6 Condition Variable Coordination", "./bin/dlock co6 condition > /dev/null 2>&1", 0);
    run_test("CO-6 POSIX Counting Semaphore", "./bin/dlock co6 semaphore > /dev/null 2>&1", 0);
    run_test("CO-6 Coffman Deadlock Safe Detection", "./bin/dlock co6 deadlock > /dev/null 2>&1", 0);

    /* Scheduling & Monitoring */
    run_test("User-Level Scheduler (FCFS, RR, Priority)", "./bin/dlock scheduler > /dev/null 2>&1", 0);
    run_test("Linux /proc System & Process Monitor", "./bin/dlock monitor > /dev/null 2>&1", 0);

    /* DLock Server-Client Integration Tests */
    printf("\n--- DLock Central Service Integration ---\n");
    pid_t server_pid = fork();
    if (server_pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execl("./bin/dlock", "./bin/dlock", "server", NULL);
        _exit(1);
    }

    usleep(250000);

    run_test("Client Ping Server", "./bin/dlock client ping CLIENT_TEST > /dev/null 2>&1", 0);
    run_test("Client Status (Initial FREE)", "./bin/dlock client status CLIENT_TEST > /dev/null 2>&1", 0);
    run_test("Client Acquire Lock (CLIENT_A)", "./bin/dlock client lock CLIENT_A > /dev/null 2>&1", 0);
    run_test("Client Write Protected Resource", "./bin/dlock client write CLIENT_A 'SafeDataRecord' > /dev/null 2>&1", 0);
    run_test("Client Read Protected Resource", "./bin/dlock client read CLIENT_A > /dev/null 2>&1", 0);
    run_test("Client Status (HELD by CLIENT_A)", "./bin/dlock client status CLIENT_TEST > /dev/null 2>&1", 0);
    run_test("Unauthorized Unlock Denial (CLIENT_B)", "./bin/dlock client unlock CLIENT_B > /dev/null 2>&1", 1);
    run_test("Authorized Unlock Release (CLIENT_A)", "./bin/dlock client unlock CLIENT_A > /dev/null 2>&1", 0);
    run_test("Client Shutdown Server", "./bin/dlock client shutdown CLIENT_ADMIN > /dev/null 2>&1", 0);

    waitpid(server_pid, NULL, 0);

    printf("\n========================================================\n");
    printf("Test Results: %d/%d passed\n", passed_tests, total_tests);
    printf("========================================================\n\n");

    return (passed_tests == total_tests) ? 0 : 1;
}
