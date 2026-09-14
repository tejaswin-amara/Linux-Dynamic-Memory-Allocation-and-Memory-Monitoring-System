# ==============================================================================
# Linux Dynamic Memory Allocation & System Task Manager (CLI + Web/GUI)
# Awesome Dev Pipeline - Systems/CLI Archetype
# KLEF 25CS2104E: Outside-In Operating Systems & Systems Programming
# ==============================================================================

CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -pedantic -std=c11 -D_GNU_SOURCE -pthread -fPIC
INC_FLAGS := -Iinclude -Itests/unity

# Targets and Directories
BUILD_DIR := build
BIN_DIR := bin
SRC_ALLOC_DIR := src/allocator
SRC_MONITOR_DIR := src/monitor
SRC_UI_DIR := src/ui
TESTS_DIR := tests

LIB_ALLOC := libmyalloc.so
BIN_MONITOR := mem_monitor
TEST_ALLOC := test_allocator
TEST_PARSER := test_proc_parser
TEST_SIGNAL := test_signal_handler

# Libraries
LDLIBS_MONITOR := -lncurses -pthread
LDLIBS_TEST := -pthread

# Sanitizer Flags
ASAN_FLAGS := -fsanitize=address,undefined -g -fno-omit-frame-pointer

.PHONY: all asan valgrind test benchmark clean help directories

all: directories $(LIB_ALLOC) $(BIN_MONITOR)

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# ------------------------------------------------------------------------------
# Module A: Custom Dynamic Memory Allocator (Shared Library)
# ------------------------------------------------------------------------------
$(LIB_ALLOC): $(SRC_ALLOC_DIR)/allocator.c $(SRC_ALLOC_DIR)/free_list.c $(SRC_ALLOC_DIR)/preload_shim.c
	@echo "==> Building Shared Library: $@"
	$(CC) $(CFLAGS) $(INC_FLAGS) -shared -o $@ $^ -ldl

# ------------------------------------------------------------------------------
# Module B & C: System Monitor & Interface Layer
# ------------------------------------------------------------------------------
$(BIN_MONITOR): $(SRC_MONITOR_DIR)/main.c $(SRC_MONITOR_DIR)/proc_parser.c \
                $(SRC_MONITOR_DIR)/signal_handler.c $(SRC_MONITOR_DIR)/gui_server.c \
                $(SRC_UI_DIR)/tui.c
	@echo "==> Building Task Manager Binary: $@"
	$(CC) $(CFLAGS) $(INC_FLAGS) -o $@ $^ $(LDLIBS_MONITOR)

# ------------------------------------------------------------------------------
# Sanitizers (ASan / UBSan)
# ------------------------------------------------------------------------------
asan: CFLAGS += $(ASAN_FLAGS)
asan: directories $(LIB_ALLOC) $(BIN_MONITOR) $(TEST_ALLOC) $(TEST_PARSER) $(TEST_SIGNAL)
	@echo "==> Built with AddressSanitizer and UndefinedBehaviorSanitizer"

# ------------------------------------------------------------------------------
# Testing Targets
# ------------------------------------------------------------------------------
$(TEST_ALLOC): $(TESTS_DIR)/test_allocator.c $(SRC_ALLOC_DIR)/allocator.c \
               $(SRC_ALLOC_DIR)/free_list.c $(TESTS_DIR)/unity/unity.c
	@echo "==> Building Allocator Unit Tests: $@"
	$(CC) $(CFLAGS) $(INC_FLAGS) -o $@ $^ $(LDLIBS_TEST)

$(TEST_PARSER): $(TESTS_DIR)/test_proc_parser.c $(SRC_MONITOR_DIR)/proc_parser.c \
                $(TESTS_DIR)/unity/unity.c
	@echo "==> Building Proc Parser Unit Tests: $@"
	$(CC) $(CFLAGS) $(INC_FLAGS) -o $@ $^ $(LDLIBS_TEST)

$(TEST_SIGNAL): $(TESTS_DIR)/test_signal_handler.c $(SRC_MONITOR_DIR)/signal_handler.c \
                $(TESTS_DIR)/unity/unity.c
	@echo "==> Building Signal Handler Unit Tests: $@"
	$(CC) $(CFLAGS) $(INC_FLAGS) -o $@ $^ $(LDLIBS_TEST)

test: $(TEST_ALLOC) $(TEST_PARSER) $(TEST_SIGNAL)
	@echo "==> Running Allocator Unit Tests..."
	./$(TEST_ALLOC)
	@echo "==> Running Proc Parser Unit Tests..."
	./$(TEST_PARSER)
	@echo "==> Running Signal Handler Unit Tests..."
	./$(TEST_SIGNAL)
	@echo "==> Running Integration Tests..."
	@bash tests/integration_test.sh

valgrind: all $(TEST_ALLOC) $(TEST_PARSER) $(TEST_SIGNAL)
	@echo "==> Running Allocator Valgrind Leak Checks..."
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_ALLOC)
	@echo "==> Running Proc Parser Valgrind Leak Checks..."
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_PARSER)
	@echo "==> Running Signal Handler Valgrind Leak Checks..."
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_SIGNAL)

benchmark: $(LIB_ALLOC)
	@echo "==> Running Allocator Benchmark vs Glibc..."
	@bash scripts/benchmark.sh

clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(LIB_ALLOC) $(BIN_MONITOR) $(TEST_ALLOC) $(TEST_PARSER) $(TEST_SIGNAL) *.o

help:
	@echo "Available Makefile targets:"
	@echo "  all        - Build libmyalloc.so and mem_monitor binary"
	@echo "  asan       - Compile with AddressSanitizer and UBSan enabled"
	@echo "  test       - Build and execute Unity unit tests and integration tests"
	@echo "  valgrind   - Verify zero leaks with Valgrind"
	@echo "  benchmark  - Execute performance benchmarks comparing with glibc"
	@echo "  clean      - Remove compiled artifacts"
