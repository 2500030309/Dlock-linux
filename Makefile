CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread -g -Iinclude -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -pthread

INC_DIR = include
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests
DEMO_DIR = demo

# Source sets
SERVER_SRCS = $(SRC_DIR)/server_main.c \
              $(SRC_DIR)/server.c \
              $(SRC_DIR)/thread_pool.c \
              $(SRC_DIR)/protocol.c \
              $(SRC_DIR)/lock_table.c \
              $(SRC_DIR)/wait_queue.c \
              $(SRC_DIR)/deadlock.c \
              $(SRC_DIR)/lease.c \
              $(SRC_DIR)/wal.c \
              $(SRC_DIR)/signals.c \
              $(SRC_DIR)/admin.c \
              $(SRC_DIR)/stats.c \
              $(SRC_DIR)/logger.c

CLIENT_SRCS = $(SRC_DIR)/client_main.c \
              $(SRC_DIR)/client_api.c \
              $(SRC_DIR)/protocol.c \
              $(SRC_DIR)/logger.c

SERVER_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SERVER_SRCS))
CLIENT_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CLIENT_SRCS))

# Binaries
SERVER_BIN = $(BIN_DIR)/dlockd
CLIENT_BIN = $(BIN_DIR)/dlockctl
PEER_BIN   = $(BIN_DIR)/dlockpeer

TEST_BINS  = $(BIN_DIR)/test_protocol \
             $(BIN_DIR)/test_lock_table \
             $(BIN_DIR)/test_wait_queue \
             $(BIN_DIR)/test_deadlock \
             $(BIN_DIR)/stress_clients

DEMO_BINS  = $(BIN_DIR)/demo_counter

.PHONY: all dirs clean test asan tsan demo peer

all: dirs $(SERVER_BIN) $(CLIENT_BIN) $(PEER_BIN) $(TEST_BINS) $(DEMO_BINS)

dirs:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR) data logs

# Compile source files to objects
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build Server Daemon (dlockd)
$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(SERVER_OBJS) $(LDFLAGS) -o $@

# Build Client CLI (dlockctl)
$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $(CLIENT_OBJS) $(LDFLAGS) -o $@

# Build Peer-to-Peer Daemon (dlockpeer)
$(PEER_BIN): $(SRC_DIR)/ricart.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

# Test Binaries
$(BIN_DIR)/test_protocol: $(TEST_DIR)/test_protocol.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BIN_DIR)/test_wait_queue: $(TEST_DIR)/test_wait_queue.c $(OBJ_DIR)/wait_queue.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BIN_DIR)/test_deadlock: $(TEST_DIR)/test_deadlock.c $(OBJ_DIR)/deadlock.o $(OBJ_DIR)/logger.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BIN_DIR)/test_lock_table: $(TEST_DIR)/test_lock_table.c $(OBJ_DIR)/lock_table.o $(OBJ_DIR)/wait_queue.o $(OBJ_DIR)/deadlock.o $(OBJ_DIR)/wal.o $(OBJ_DIR)/stats.o $(OBJ_DIR)/logger.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BIN_DIR)/stress_clients: $(TEST_DIR)/stress_clients.c $(SRC_DIR)/client_api.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# Demo Binaries
$(BIN_DIR)/demo_counter: $(DEMO_DIR)/demo_counter.c $(SRC_DIR)/client_api.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# AddressSanitizer target
asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean all

# ThreadSanitizer target
tsan: CFLAGS += -fsanitize=thread
tsan: LDFLAGS += -fsanitize=thread
tsan: clean all

# Run test suite
test: all
	@chmod +x $(TEST_DIR)/run_tests.sh
	@bash $(TEST_DIR)/run_tests.sh

# Run demo counter
demo: all
	@chmod +x $(DEMO_DIR)/demo_deadlock.sh $(DEMO_DIR)/demo_lease.sh
	@echo "Starting DLock daemon for demo on port 7070..."
	@$(SERVER_BIN) -p 7070 & SERVER_PID=$$!; \
	sleep 1; \
	$(BIN_DIR)/demo_counter 7070; \
	kill -TERM $$SERVER_PID 2>/dev/null || true; \
	wait $$SERVER_PID 2>/dev/null || true

clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/*
	rm -f data/*.wal data/*.dat data/*.tmp
	rm -f logs/*.log
	rm -f /tmp/dlock* /tmp/alice* /tmp/bob*
