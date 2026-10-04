#include "dlock.h"
#include "monitor.h"

#include <dirent.h>
#include <ctype.h>

static void inspect_pid(const char *pid_str) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/%s/status", pid_str);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("  Process PID %s not found or terminated.\n", pid_str);
        return;
    }

    char line[256];
    char name[64] = "unknown";
    char state[64] = "unknown";
    char ppid[32] = "unknown";
    char threads[32] = "1";
    char vmsize[64] = "-";
    char vmrss[64] = "-";

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Name:", 5) == 0) sscanf(line + 5, "%s", name);
        else if (strncmp(line, "State:", 6) == 0) sscanf(line + 6, "%s", state);
        else if (strncmp(line, "PPid:", 5) == 0) sscanf(line + 5, "%s", ppid);
        else if (strncmp(line, "Threads:", 8) == 0) sscanf(line + 8, "%s", threads);
        else if (strncmp(line, "VmSize:", 7) == 0) sscanf(line + 7, "%s", vmsize);
        else if (strncmp(line, "VmRSS:", 6) == 0) sscanf(line + 6, "%s", vmrss);
    }
    fclose(fp);

    printf("  %-7s %-6s %-16s %-8s %-8s %-10s %-10s\n",
           pid_str, ppid, name, state, threads, vmsize, vmrss);
}

int demo_monitor(void) {
    printf("\n%s%s=== Real-Time Linux /proc Process & System Monitor ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    char target_pid[32] = "";

    if (is_interactive_tty()) {
        char line[64];
        printf("Enter PID to inspect (or press Enter for active process table): ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            sscanf(line, "%31s", target_pid);
        }
    }

    printf("\n  %-7s %-6s %-16s %-8s %-8s %-10s %-10s\n",
           "PID", "PPID", "Command", "State", "Threads", "VmSize", "VmRSS");
    printf("  ----------------------------------------------------------------------\n");

    if (strlen(target_pid) > 0) {
        inspect_pid(target_pid);
    } else {
        /* Show self and parent */
        char self_pid[32], parent_pid[32];
        snprintf(self_pid, sizeof(self_pid), "%d", getpid());
        snprintf(parent_pid, sizeof(parent_pid), "%d", getppid());
        inspect_pid(self_pid);
        inspect_pid(parent_pid);

        /* Show up to 6 other running processes */
        DIR *dir = opendir("/proc");
        if (dir) {
            struct dirent *ent;
            int count = 0;
            while ((ent = readdir(dir)) != NULL && count < 6) {
                if (isdigit(ent->d_name[0])) {
                    if (strcmp(ent->d_name, self_pid) != 0 && strcmp(ent->d_name, parent_pid) != 0) {
                        inspect_pid(ent->d_name);
                        count++;
                    }
                }
            }
            closedir(dir);
        }
    }
    printf("  ----------------------------------------------------------------------\n\n");
    return 0;
}
