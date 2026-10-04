#!/bin/bash
# ==============================================================================
# DLock Live Interactive Project Review Demonstration Script
# For Ubuntu App / Linux Terminal
# ==============================================================================

set -e

# Change to project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "\033[1;36m===============================================================\033[0m"
echo -e "\033[1;36m  DLOCK LIVE OPERATING SYSTEMS PROJECT REVIEW DEMONSTRATION     \033[0m"
echo -e "\033[1;36m  Running on Ubuntu / Linux                                     \033[0m"
echo -e "\033[1;36m===============================================================\033[0m"
echo ""

# 1. Compile the project
echo -e "\033[1;33m[STEP 1] Compiling project with GNU Make...\033[0m"
make all
echo ""

# 2. Run automated test suite
echo -e "\033[1;33m[STEP 2] Running complete automated verification suite (31 tests)...\033[0m"
./bin/test_dlock
echo ""

# 3. Demonstrate Course Outcomes
echo -e "\033[1;33m[STEP 3] Demonstrating CO-1: Syscall & Command Execution Journey...\033[0m"
./bin/dlock co1
echo ""

echo -e "\033[1;33m[STEP 4] Demonstrating CO-4: Copy-On-Write (COW)...\033[0m"
./bin/dlock co4 cow
echo ""

echo -e "\033[1;33m[STEP 5] Demonstrating CO-5: Inode & Filesystem Abstraction...\033[0m"
./bin/dlock co5 filesystem
echo ""

echo -e "\033[1;33m[STEP 6] Demonstrating CO-6: Mutual Exclusion via POSIX Mutex...\033[0m"
./bin/dlock co6 mutex
echo ""

echo -e "\033[1;33m[STEP 7] Demonstrating CPU Scheduling Simulation (FCFS, RR, Priority)...\033[0m"
./bin/dlock scheduler
echo ""

# 4. Central DLock Server & Multi-Client Mutual Exclusion Simulation
echo -e "\033[1;33m[STEP 8] Starting DLock Central Server in background...\033[0m"
./bin/dlock server &
SERVER_PID=$!
sleep 0.5
echo ""

echo -e "\033[1;33m[STEP 9] Client A requesting exclusive lock...\033[0m"
./bin/dlock client lock CLIENT_A
sleep 0.5
echo ""

echo -e "\033[1;33m[STEP 10] Checking Server Status...\033[0m"
./bin/dlock client status CLIENT_MONITOR
sleep 0.5
echo ""

echo -e "\033[1;33m[STEP 11] Client B requesting lock (will be enqueued in background)...\033[0m"
./bin/dlock client lock CLIENT_B &
CLIENT_B_PID=$!
sleep 0.5
echo ""

echo -e "\033[1;33m[STEP 12] Unauthorized unlock attempt by CLIENT_C (Expect DENIED)...\033[0m"
./bin/dlock client unlock CLIENT_C || true
sleep 0.5
echo ""

echo -e "\033[1;33m[STEP 13] Client A releasing lock (handover to Client B)...\033[0m"
./bin/dlock client unlock CLIENT_A
wait $CLIENT_B_PID
sleep 0.5
echo ""

echo -e "\033[1;33m[STEP 14] Client B releasing lock...\033[0m"
./bin/dlock client unlock CLIENT_B
sleep 0.5
echo ""

echo -e "\033[1;33m[STEP 15] Inspecting Linux Process Monitor (/proc)...\033[0m"
./bin/dlock monitor
echo ""

echo -e "\033[1;33m[STEP 16] Gracefully shutting down DLock Server...\033[0m"
./bin/dlock client shutdown CLIENT_ADMIN
wait $SERVER_PID 2>/dev/null || true
echo ""

echo -e "\033[1;32m===============================================================\033[0m"
echo -e "\033[1;32m  ALL DEMONSTRATIONS COMPLETED SUCCESSFULLY (10/10 READY)      \033[0m"
echo -e "\033[1;32m===============================================================\033[0m"
