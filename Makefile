# ==============================================================================
# Linux Dynamic Memory Allocation & System Task Manager (CLI + Web/GUI)
# KLEF 25CS2104E: Outside-In Operating Systems & Systems Programming
# ==============================================================================

CC ?= gcc
BASE_CFLAGS := -Wall -Wextra -Werror -pedantic -std=c11 -D_GNU_SOURCE -pthread
CFLAGS ?= -g -O2
INC_FLAGS := -Iinclude -Isrc/allocator -Itests/unity
DEP_FLAGS := -MMD -MP

# Sanitizer Flags
ASAN_FLAGS := -fsanitize=address,undefined -g -fno-omit-frame-pointer

# Determine Active Build Mode
BUILD_MODE ?= release
ifeq ($(filter asan,$(MAKECMDGOALS)),asan)
  BUILD_MODE := asan
endif

ifeq ($(BUILD_MODE),asan)
  MODE_CFLAGS := $(ASAN_FLAGS)
  BUILD_DIR := build/asan
else
  MODE_CFLAGS :=
  BUILD_DIR := build
endif

ALL_CFLAGS = $(BASE_CFLAGS) $(CFLAGS) $(MODE_CFLAGS) $(INC_FLAGS) $(DEP_FLAGS)
SHLIB_CFLAGS = $(ALL_CFLAGS) -fPIC -fvisibility=hidden

# Targets and Directories
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
TEST_GUI := test_gui_server

# Libraries
LDLIBS_SHLIB := -ldl
LDLIBS_MONITOR := -lncurses -pthread
LDLIBS_TEST := -pthread -lm

# Mode Sentinel Tracking
MODE_FILE := build/.mode
ACTIVE_MODE := $(shell cat $(MODE_FILE) 2>/dev/null)
ifneq ($(ACTIVE_MODE),$(BUILD_MODE))
  $(shell mkdir -p build && echo $(BUILD_MODE) > $(MODE_FILE))
endif

# Source and Object Definitions
LIB_SRCS := $(SRC_ALLOC_DIR)/allocator.c \
            $(SRC_ALLOC_DIR)/free_list.c \
            $(SRC_ALLOC_DIR)/preload_shim.c
LIB_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.pic.o,$(LIB_SRCS))

MONITOR_SRCS := $(SRC_MONITOR_DIR)/main.c \
                $(SRC_MONITOR_DIR)/proc_parser.c \
                $(SRC_MONITOR_DIR)/signal_handler.c \
                $(SRC_MONITOR_DIR)/gui_server.c \
                $(SRC_UI_DIR)/tui.c
MONITOR_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(MONITOR_SRCS))

TEST_UNITY_OBJ := $(BUILD_DIR)/$(TESTS_DIR)/unity/unity.o

TEST_ALLOC_OBJS := $(BUILD_DIR)/$(TESTS_DIR)/test_allocator.o \
                   $(TEST_UNITY_OBJ) \
                   $(BUILD_DIR)/$(SRC_ALLOC_DIR)/allocator.o \
                   $(BUILD_DIR)/$(SRC_ALLOC_DIR)/free_list.o

TEST_PARSER_OBJS := $(BUILD_DIR)/$(TESTS_DIR)/test_proc_parser.o \
                    $(TEST_UNITY_OBJ) \
                    $(BUILD_DIR)/$(SRC_MONITOR_DIR)/proc_parser.o

TEST_SIGNAL_OBJS := $(BUILD_DIR)/$(TESTS_DIR)/test_signal_handler.o \
                    $(TEST_UNITY_OBJ) \
                    $(BUILD_DIR)/$(SRC_MONITOR_DIR)/signal_handler.o

TEST_GUI_OBJS := $(BUILD_DIR)/$(TESTS_DIR)/test_gui_server.o \
                 $(TEST_UNITY_OBJ) \
                 $(BUILD_DIR)/$(SRC_MONITOR_DIR)/gui_server.o \
                 $(BUILD_DIR)/$(SRC_MONITOR_DIR)/signal_handler.o

ALL_OBJS := $(sort $(MONITOR_OBJS) $(TEST_ALLOC_OBJS) $(TEST_PARSER_OBJS) \
                   $(TEST_SIGNAL_OBJS) $(TEST_GUI_OBJS))
ALL_DEPS := $(ALL_OBJS:.o=.d) $(LIB_OBJS:.pic.o=.pic.d)

.PHONY: all asan valgrind soak test benchmark clean help directories

all: directories $(LIB_ALLOC) $(BIN_MONITOR)

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# ------------------------------------------------------------------------------
# Pattern Rules for Objects (M1.1)
# ------------------------------------------------------------------------------
$(BUILD_DIR)/%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SHLIB_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# ------------------------------------------------------------------------------
# Binaries and Shared Libraries
# ------------------------------------------------------------------------------
$(LIB_ALLOC): $(LIB_OBJS) $(MODE_FILE)
	@echo "==> Linking Shared Library: $@"
	$(CC) $(BASE_CFLAGS) $(CFLAGS) $(MODE_CFLAGS) -shared -o $@ $(LIB_OBJS) $(LDLIBS_SHLIB)

$(BIN_MONITOR): $(MONITOR_OBJS) $(MODE_FILE)
	@echo "==> Linking Task Manager Binary: $@"
	$(CC) $(BASE_CFLAGS) $(CFLAGS) $(MODE_CFLAGS) -o $@ $(MONITOR_OBJS) $(LDLIBS_MONITOR)

# ------------------------------------------------------------------------------
# Testing Targets (M1.2 & M1.3)
# ------------------------------------------------------------------------------
$(TEST_ALLOC): $(TEST_ALLOC_OBJS) $(MODE_FILE)
	@echo "==> Linking Allocator Unit Tests: $@"
	$(CC) $(BASE_CFLAGS) $(CFLAGS) $(MODE_CFLAGS) -o $@ $(TEST_ALLOC_OBJS) $(LDLIBS_TEST)

$(TEST_PARSER): $(TEST_PARSER_OBJS) $(MODE_FILE)
	@echo "==> Linking Proc Parser Unit Tests: $@"
	$(CC) $(BASE_CFLAGS) $(CFLAGS) $(MODE_CFLAGS) -o $@ $(TEST_PARSER_OBJS) $(LDLIBS_TEST)

$(TEST_SIGNAL): $(TEST_SIGNAL_OBJS) $(MODE_FILE)
	@echo "==> Linking Signal Handler Unit Tests: $@"
	$(CC) $(BASE_CFLAGS) $(CFLAGS) $(MODE_CFLAGS) -o $@ $(TEST_SIGNAL_OBJS) $(LDLIBS_TEST)

$(TEST_GUI): $(TEST_GUI_OBJS) $(MODE_FILE)
	@echo "==> Linking GUI Server Unit Tests: $@"
	$(CC) $(BASE_CFLAGS) $(CFLAGS) $(MODE_CFLAGS) -o $@ $(TEST_GUI_OBJS) $(LDLIBS_TEST)

test: all $(TEST_ALLOC) $(TEST_PARSER) $(TEST_SIGNAL) $(TEST_GUI)
	@echo "==> Running Allocator Unit Tests..."
	./$(TEST_ALLOC)
	@echo "==> Running Proc Parser Unit Tests..."
	./$(TEST_PARSER)
	@echo "==> Running Signal Handler Unit Tests..."
	./$(TEST_SIGNAL)
	@echo "==> Running GUI Server Unit Tests..."
	./$(TEST_GUI)
	@echo "==> Running XSS Security Tests..."
	@if command -v node >/dev/null 2>&1; then \
		node tests/test_xss.js; \
	elif command -v python3 >/dev/null 2>&1; then \
		python3 tests/test_xss.py; \
	else \
		echo "[-] WARNING: Neither node nor python3 found; skipping XSS DOM security test"; \
	fi
	@echo "==> Running Integration Tests..."
	@bash tests/integration_test.sh

# ------------------------------------------------------------------------------
# Sanitizers (ASan / UBSan) (M1.4)
# ------------------------------------------------------------------------------
asan: directories $(LIB_ALLOC) $(BIN_MONITOR) $(TEST_ALLOC) $(TEST_PARSER) $(TEST_SIGNAL) $(TEST_GUI)
	@echo "==> Built with AddressSanitizer and UndefinedBehaviorSanitizer"

valgrind: all $(TEST_ALLOC) $(TEST_PARSER) $(TEST_SIGNAL) $(TEST_GUI)
	@echo "==> Running Allocator Valgrind Leak Checks..."
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_ALLOC)
	@echo "==> Running Proc Parser Valgrind Leak Checks..."
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_PARSER)
	@echo "==> Running Signal Handler Valgrind Leak Checks..."
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_SIGNAL)
	@echo "==> Running GUI Server Valgrind Leak Checks..."
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_GUI)
	@echo "==> Running mem_monitor under Valgrind (Memcheck's allocator replaces libmyalloc.so in this run; see scripts/selfhosted_soak.sh for the allocator soak)"
	@bash scripts/valgrind_soak.sh

soak: all
	@echo "==> Running libmyalloc.so allocator soak without Valgrind..."
	@bash scripts/selfhosted_soak.sh

benchmark: $(LIB_ALLOC)
	@echo "==> Running Allocator Benchmark vs Glibc..."
	@bash scripts/benchmark.sh

clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf build bin $(LIB_ALLOC) $(BIN_MONITOR) $(TEST_ALLOC) $(TEST_PARSER) $(TEST_SIGNAL) $(TEST_GUI) *.o *.d

help:
	@echo "Available Makefile targets:"
	@echo "  all        - Build libmyalloc.so and mem_monitor binary (release mode)"
	@echo "  asan       - Compile with AddressSanitizer and UBSan into build/asan"
	@echo "  test       - Build all targets and execute unit tests, XSS check, and integration tests"
	@echo "  valgrind   - Run Memcheck suite"
	@echo "  soak       - Run mem_monitor soak test under HTTP load"
	@echo "  benchmark  - Execute performance benchmarks comparing with glibc"
	@echo "  clean      - Remove compiled artifacts"

-include $(ALL_DEPS)
