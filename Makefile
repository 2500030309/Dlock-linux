CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
DEBUG_FLAGS = -g -O0 -DDEBUG
RELEASE_FLAGS = -O2

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin
RUNTIME_DIR = runtime
LOGS_DIR = $(RUNTIME_DIR)/logs
DATA_DIR = $(RUNTIME_DIR)/data
TEST_DIR = tests

SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/server.c \
       $(SRC_DIR)/client.c \
       $(SRC_DIR)/process.c \
       $(SRC_DIR)/ipc.c \
       $(SRC_DIR)/memory.c \
       $(SRC_DIR)/filesystem.c \
       $(SRC_DIR)/concurrency.c \
       $(SRC_DIR)/scheduler.c \
       $(SRC_DIR)/monitor.c

OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = $(BIN_DIR)/dlock
TEST_TARGET = $(BIN_DIR)/test_dlock

.PHONY: all clean test run debug dirs

all: dirs $(TARGET) $(TEST_TARGET)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) $(LOGS_DIR) $(DATA_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

$(TEST_TARGET): $(TEST_DIR)/test_dlock.c $(TARGET)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_dlock.c -o $@

debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean all

run: all
	@./$(TARGET)

test: all
	@./$(TEST_TARGET)

clean:
	@rm -rf $(OBJ_DIR) $(BIN_DIR) $(RUNTIME_DIR)
	@rm -f /tmp/dlock_*.fifo 2>/dev/null || true
	@echo "Clean completed."
