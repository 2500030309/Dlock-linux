#!/usr/bin/env bash
set -e

PORT=7373
CTL="/tmp/dlock_demo_lease.ctl"
WAL="data/demo_lease.wal"
LOG="logs/demo_lease.log"

rm -f "$CTL" "$WAL" "$LOG"

echo "=========================================================="
echo "          DLock Lease Expiration & Crash Recovery Demo     "
echo "=========================================================="

echo "[1] Starting DLock server on port $PORT..."
./bin/dlockd -p $PORT -a "$CTL" -w "$WAL" -l "$LOG" &
SERVER_PID=$!
sleep 1

A_FIFO="/tmp/client_a.in"
B_FIFO="/tmp/client_b.in"
rm -f "$A_FIFO" "$B_FIFO"
mkfifo "$A_FIFO" "$B_FIFO"

./bin/dlockctl -p $PORT -i client_a < "$A_FIFO" > /tmp/client_a.out 2>&1 &
A_PID=$!

./bin/dlockctl -p $PORT -i client_b < "$B_FIFO" > /tmp/client_b.out 2>&1 &
B_PID=$!

exec 3> "$A_FIFO"
exec 4> "$B_FIFO"

sleep 0.5

echo "[2] Client A acquires 'shared_db' with 3000ms lease..."
echo "lock shared_db 0 3000" >&3
sleep 0.5
cat /tmp/client_a.out

echo "[3] Client B requests 'shared_db' with 6000ms timeout (Client B blocks waiting in FIFO queue)..."
echo "lock shared_db 6000 5000" >&4
sleep 0.5

echo "[4] Simulating catastrophic failure: killing Client A with SIGKILL (kill -9)..."
kill -9 "$A_PID" 2>/dev/null || true
exec 3>&- 2>/dev/null || true
echo "  -> Client A process terminated abruptly."

echo "[5] Waiting for DLock lease / disconnect handling to wake Client B..."
sleep 1.5

echo "[6] Inspecting Client B output:"
cat /tmp/client_b.out

if grep -q "OK" /tmp/client_b.out; then
    echo "=========================================================="
    echo " SUCCESS: Lock reclaimed and granted to waiting Client B!  "
    echo "=========================================================="
else
    echo "FAILED: Client B did not receive lock grant!"
fi

# Clean up
exec 4>&- 2>/dev/null || true
kill -9 "$A_PID" "$B_PID" 2>/dev/null || true
kill -TERM "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
rm -f "$A_FIFO" "$B_FIFO" /tmp/client_a.out /tmp/client_b.out "$CTL" "$WAL"
exit 0
