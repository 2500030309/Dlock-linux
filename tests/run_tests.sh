#!/usr/bin/env bash
set -e

PORT=7171
WAL_FILE="data/test_run.wal"
CTL_FILE="/tmp/dlock_test.ctl"
LOG_FILE="logs/test_run.log"

SETARCH=""
if command -v setarch >/dev/null 2>&1; then
    SETARCH="setarch $(uname -m) -R"
fi

echo "==========================================="
echo "       DLock Comprehensive Test Suite       "
echo "==========================================="

echo "[1/5] Running protocol unit tests..."
$SETARCH ./bin/test_protocol
echo "PASS: test_protocol"

echo "[2/5] Running wait_queue unit tests..."
$SETARCH ./bin/test_wait_queue
echo "PASS: test_wait_queue"

echo "[3/5] Running deadlock unit tests..."
$SETARCH ./bin/test_deadlock
echo "PASS: test_deadlock"

echo "[4/5] Running lock_table unit tests..."
$SETARCH ./bin/test_lock_table
echo "PASS: test_lock_table"

echo "[5/5] Running multiprocess stress test..."
# Clean old test files
rm -f "$WAL_FILE" "$CTL_FILE" "$LOG_FILE"

# Start background server
$SETARCH ./bin/dlockd -p $PORT -a "$CTL_FILE" -w "$WAL_FILE" -l "$LOG_FILE" &
SERVER_PID=$!

# Give server time to bind and listen
sleep 1

# Run stress test against server
$SETARCH ./bin/stress_clients 10 15 $PORT
STRESS_RC=$?

# Graceful server shutdown via SIGTERM
kill -TERM "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

# Cleanup test files
rm -f "$WAL_FILE" "$CTL_FILE"

if [ $STRESS_RC -eq 0 ]; then
    echo "==========================================="
    echo "       ALL TESTS PASSED SUCCESSFULLY       "
    echo "==========================================="
    exit 0
else
    echo "==========================================="
    echo "       STRESS TEST FAILED                  "
    echo "==========================================="
    exit 1
fi
