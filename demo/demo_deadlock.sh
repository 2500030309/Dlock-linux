#!/usr/bin/env bash
set -e

PORT=7272
CTL="/tmp/dlock_demo_dl.ctl"
WAL="data/demo_deadlock.wal"
LOG="logs/demo_deadlock.log"

rm -f "$CTL" "$WAL" "$LOG"

echo "=========================================================="
echo "         DLock Deadlock Cycle Detection Demonstration     "
echo "=========================================================="

echo "[1] Starting DLock server on port $PORT..."
./bin/dlockd -p $PORT -a "$CTL" -w "$WAL" -l "$LOG" &
SERVER_PID=$!
sleep 1

echo "[2] Client 1 (Alice) connects and acquires lock 'resA'..."
# Open interactive sessions or FIFO pipes to communicate interactively
ALICE_FIFO="/tmp/alice.in"
BOB_FIFO="/tmp/bob.in"
rm -f "$ALICE_FIFO" "$BOB_FIFO"
mkfifo "$ALICE_FIFO" "$BOB_FIFO"

# Spawn background client processes
./bin/dlockctl -p $PORT -i alice < "$ALICE_FIFO" > /tmp/alice.out 2>&1 &
ALICE_PID=$!
./bin/dlockctl -p $PORT -i bob < "$BOB_FIFO" > /tmp/bob.out 2>&1 &
BOB_PID=$!

# Keep FIFOs open so processes do not receive EOF
exec 3> "$ALICE_FIFO"
exec 4> "$BOB_FIFO"

sleep 0.5

# Step 1: Alice locks resA
echo "lock resA 5000" >&3
sleep 0.5
echo "  -> Alice acquired resA"

# Step 2: Bob locks resB
echo "lock resB 5000" >&4
sleep 0.5
echo "  -> Bob acquired resB"

# Step 3: Bob attempts to lock resA (held by Alice) with 5s timeout -> Bob blocks!
echo "lock resA 5000" >&4
sleep 0.5
echo "  -> Bob requested resA (currently waiting on Alice: Bob -> Alice)"

# Step 4: Alice attempts to lock resB (held by Bob) -> creates Alice -> Bob -> Alice cycle!
echo "[3] Alice requests 'resB' (Wait-For Graph cycle: Alice -> Bob -> Alice)..."
echo "lock resB 5000" >&3
sleep 0.5

# Step 5: Check output from Alice
echo "[4] Inspecting Alice's response from server:"
tail -n 5 /tmp/alice.out

if grep -q "ERR DEADLOCK" /tmp/alice.out; then
    echo "=========================================================="
    echo " SUCCESS: Server detected cycle and returned ERR DEADLOCK!"
    echo "=========================================================="
else
    echo "FAILED to detect deadlock!"
fi

# Clean up
exec 3>&- 2>/dev/null || true
exec 4>&- 2>/dev/null || true
kill -9 "$ALICE_PID" "$BOB_PID" 2>/dev/null || true
kill -TERM "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
rm -f "$ALICE_FIFO" "$BOB_FIFO" /tmp/alice.out /tmp/bob.out "$CTL" "$WAL"
exit 0
