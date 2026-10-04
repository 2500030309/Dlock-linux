#include "dlock.h"
#include "scheduler.h"

typedef struct {
    char name[16];
    int arrival_time;
    int burst_time;
    int remaining_time;
    int priority;
    int completion_time;
    int turnaround_time;
    int waiting_time;
} sim_process_t;

/* --- 1. First-Come First-Served (FCFS) --- */
static void run_fcfs(sim_process_t procs[], int n) {
    printf("%s[1. First-Come, First-Served (FCFS) Scheduling]%s\n", COLOR_YELLOW, COLOR_RESET);
    int current_time = 0;
    double total_tat = 0, total_wt = 0;

    printf("  Gantt Chart: |");
    for (int i = 0; i < n; i++) {
        if (current_time < procs[i].arrival_time) {
            current_time = procs[i].arrival_time;
        }
        printf(" %s (%d-%d) |", procs[i].name, current_time, current_time + procs[i].burst_time);
        current_time += procs[i].burst_time;
        procs[i].completion_time = current_time;
        procs[i].turnaround_time = procs[i].completion_time - procs[i].arrival_time;
        procs[i].waiting_time = procs[i].turnaround_time - procs[i].burst_time;
        total_tat += procs[i].turnaround_time;
        total_wt += procs[i].waiting_time;
    }
    printf("\n\n");

    printf("  %-8s %-4s %-4s %-4s %-4s %-4s\n", "Process", "AT", "BT", "CT", "TAT", "WT");
    printf("  --------------------------------------\n");
    for (int i = 0; i < n; i++) {
        printf("  %-8s %-4d %-4d %-4d %-4d %-4d\n",
               procs[i].name, procs[i].arrival_time, procs[i].burst_time,
               procs[i].completion_time, procs[i].turnaround_time, procs[i].waiting_time);
    }
    printf("  Average Turnaround Time = %.2f\n", total_tat / n);
    printf("  Average Waiting Time    = %.2f\n\n", total_wt / n);
}

/* --- 2. Round Robin (RR) --- */
static void run_round_robin(sim_process_t procs[], int n, int quantum) {
    printf("%s[2. Round Robin (RR) Scheduling - Time Quantum = %d]%s\n", COLOR_YELLOW, quantum, COLOR_RESET);
    int current_time = 0;
    int completed = 0;
    for (int i = 0; i < n; i++) procs[i].remaining_time = procs[i].burst_time;

    printf("  Gantt Chart: |");
    while (completed < n) {
        int progress = 0;
        for (int i = 0; i < n; i++) {
            if (procs[i].remaining_time > 0 && procs[i].arrival_time <= current_time) {
                progress = 1;
                int slice = (procs[i].remaining_time > quantum) ? quantum : procs[i].remaining_time;
                printf(" %s (%d-%d) |", procs[i].name, current_time, current_time + slice);
                current_time += slice;
                procs[i].remaining_time -= slice;
                if (procs[i].remaining_time == 0) {
                    completed++;
                    procs[i].completion_time = current_time;
                    procs[i].turnaround_time = procs[i].completion_time - procs[i].arrival_time;
                    procs[i].waiting_time = procs[i].turnaround_time - procs[i].burst_time;
                }
            }
        }
        if (!progress) current_time++;
    }
    printf("\n\n");

    double total_tat = 0, total_wt = 0;
    printf("  %-8s %-4s %-4s %-4s %-4s %-4s\n", "Process", "AT", "BT", "CT", "TAT", "WT");
    printf("  --------------------------------------\n");
    for (int i = 0; i < n; i++) {
        total_tat += procs[i].turnaround_time;
        total_wt += procs[i].waiting_time;
        printf("  %-8s %-4d %-4d %-4d %-4d %-4d\n",
               procs[i].name, procs[i].arrival_time, procs[i].burst_time,
               procs[i].completion_time, procs[i].turnaround_time, procs[i].waiting_time);
    }
    printf("  Average Turnaround Time = %.2f\n", total_tat / n);
    printf("  Average Waiting Time    = %.2f\n\n", total_wt / n);
}

/* --- 3. Non-Preemptive Priority Scheduling --- */
static void run_priority(sim_process_t procs[], int n) {
    printf("%s[3. Priority Scheduling - Lower Number = Higher Priority]%s\n", COLOR_YELLOW, COLOR_RESET);
    int current_time = 0, completed = 0;
    int is_done[32] = {0};
    double total_tat = 0, total_wt = 0;

    printf("  Gantt Chart: |");
    while (completed < n) {
        int best_idx = -1;
        int highest_pri = 999999;

        for (int i = 0; i < n; i++) {
            if (!is_done[i] && procs[i].arrival_time <= current_time) {
                if (procs[i].priority < highest_pri) {
                    highest_pri = procs[i].priority;
                    best_idx = i;
                }
            }
        }

        if (best_idx == -1) {
            current_time++;
            continue;
        }

        printf(" %s (P:%d) (%d-%d) |", procs[best_idx].name, procs[best_idx].priority,
               current_time, current_time + procs[best_idx].burst_time);
        current_time += procs[best_idx].burst_time;
        procs[best_idx].completion_time = current_time;
        procs[best_idx].turnaround_time = procs[best_idx].completion_time - procs[best_idx].arrival_time;
        procs[best_idx].waiting_time = procs[best_idx].turnaround_time - procs[best_idx].burst_time;
        total_tat += procs[best_idx].turnaround_time;
        total_wt += procs[best_idx].waiting_time;
        is_done[best_idx] = 1;
        completed++;
    }
    printf("\n\n");

    printf("  %-8s %-4s %-4s %-4s %-4s %-4s %-4s\n", "Process", "Pri", "AT", "BT", "CT", "TAT", "WT");
    printf("  ----------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        printf("  %-8s %-4d %-4d %-4d %-4d %-4d %-4d\n",
               procs[i].name, procs[i].priority, procs[i].arrival_time, procs[i].burst_time,
               procs[i].completion_time, procs[i].turnaround_time, procs[i].waiting_time);
    }
    printf("  Average Turnaround Time = %.2f\n", total_tat / n);
    printf("  Average Waiting Time    = %.2f\n\n", total_wt / n);
}

int demo_scheduler(void) {
    printf("\n%s%s=== DLock CPU Process Scheduling Simulator ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    sim_process_t procs[16];
    int n = 4;
    int quantum = 2;

    if (is_interactive_tty()) {
        printf("Choose Mode:\n");
        printf("  1. Interactive Mode (Enter custom processes & timing at runtime)\n");
        printf("  2. Standard Benchmark Workload (Default)\n");
        printf("Enter choice [1-2] (default: 2): ");
        fflush(stdout);

        char line[64];
        if (fgets(line, sizeof(line), stdin) && (line[0] == '1')) {
            printf("\nEnter number of processes (2 to 8): ");
            fflush(stdout);
            if (fgets(line, sizeof(line), stdin)) {
                int user_n = atoi(line);
                if (user_n >= 2 && user_n <= 8) n = user_n;
            }

            for (int i = 0; i < n; i++) {
                snprintf(procs[i].name, sizeof(procs[i].name), "P%d", i + 1);
                printf("\n--- Process %s ---\n", procs[i].name);

                printf("  Arrival Time (e.g. 0, 1, 2) [default %d]: ", i);
                fflush(stdout);
                if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
                    procs[i].arrival_time = atoi(line);
                } else {
                    procs[i].arrival_time = i;
                }

                printf("  Burst Time (CPU execution units) [default %d]: ", (i % 3) + 2);
                fflush(stdout);
                if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
                    procs[i].burst_time = atoi(line);
                    if (procs[i].burst_time <= 0) procs[i].burst_time = 2;
                } else {
                    procs[i].burst_time = (i % 3) + 2;
                }

                printf("  Priority (1 = Highest Priority) [default %d]: ", (n - i));
                fflush(stdout);
                if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
                    procs[i].priority = atoi(line);
                } else {
                    procs[i].priority = (n - i);
                }
            }

            printf("\nEnter Time Quantum for Round Robin [default 2]: ");
            fflush(stdout);
            if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
                int q = atoi(line);
                if (q > 0) quantum = q;
            }
            printf("\n");
        } else {
            /* Standard benchmark */
            sim_process_t default_procs[4] = {
                {"P1", 0, 5, 5, 2, 0, 0, 0},
                {"P2", 1, 3, 3, 1, 0, 0, 0},
                {"P3", 2, 4, 4, 3, 0, 0, 0},
                {"P4", 4, 2, 2, 4, 0, 0, 0}
            };
            memcpy(procs, default_procs, sizeof(default_procs));
        }
    } else {
        /* Automated script fallback */
        sim_process_t default_procs[4] = {
            {"P1", 0, 5, 5, 2, 0, 0, 0},
            {"P2", 1, 3, 3, 1, 0, 0, 0},
            {"P3", 2, 4, 4, 3, 0, 0, 0},
            {"P4", 4, 2, 2, 4, 0, 0, 0}
        };
        memcpy(procs, default_procs, sizeof(default_procs));
    }

    sim_process_t copy[16];

    memcpy(copy, procs, sizeof(procs));
    run_fcfs(copy, n);

    memcpy(copy, procs, sizeof(procs));
    run_round_robin(copy, n, quantum);

    memcpy(copy, procs, sizeof(procs));
    run_priority(copy, n);

    return 0;
}
