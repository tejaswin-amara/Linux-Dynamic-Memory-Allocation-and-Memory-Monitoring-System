# Contributing Guidelines

Thank you for contributing to the **Linux Dynamic Memory Allocation & System Task Manager** project. This project is developed to the rigorous standards of a production-grade systems software capstone (**KLEF 25CS2104E: Outside-In Operating Systems & Systems Programming**).

All contributions—whether bug fixes, architectural enhancements, performance optimizations, or documentation—must adhere to the systems engineering standards detailed below.

---

## 1. Core Engineering Tenets

1. **Defensive Systems Programming**:
   - Check **every single syscall return value** against failure (`-1` or `MAP_FAILED`).
   - Propagate and log meaningful diagnostic errors with `errno` and strerror context using `LOG_ERROR(...)`.
   - Never leave file descriptors dangling: pair every `open(2)` with a deterministic `close(2)`.

2. **Zero-Allocation Telemetry Loop**:
   - The monitoring engine (`src/monitor/proc_parser.c`) MUST NOT allocate dynamic heap memory (`malloc`/`calloc`/`realloc`) in its steady-state sampling loop.
   - Use fixed-size, stack-allocated or pre-allocated static buffers (`buf[4096]`) and direct unbuffered POSIX syscalls (`open(2)`, `read(2)`, `close(2)`).

3. **Memory Integrity & Safety**:
   - Maintain 16-byte memory payload alignment via the `ALIGN(x)` macro.
   - Preserve boundary canary words (`ALLOC_MAGIC_HEADER = 0xDEADBEEF`, `ALLOC_MAGIC_FOOTER = 0xBEEFDEAD`) across all chunk splits and merges.
   - Guard shared state: heap mutations must hold `alloc_mutex` (`pthread_mutex_t`), and snapshot telemetry exchanges must hold `snapshot_lock` (`pthread_rwlock_t`).

4. **Minimal Code (Ponytail Doctrine)**:
   - Prefer the shortest working diff. Implement only what is strictly necessary to solve the issue.
   - Avoid speculative abstractions, unnecessary layers, and external dependencies.

---

## 2. Toolchain & Environment Requirements

Development is targeted for **Ubuntu 24.04 LTS (x86_64)** with standard POSIX toolchains:

```bash
# Install core build and verification dependencies
sudo apt-get update
sudo apt-get install -y build-essential gcc make valgrind libncurses-dev clang-format curl
```

- **C Standard**: C11 (`-std=c11`) with POSIX.1-2008 extensions (`-D_GNU_SOURCE`).
- **Compiler Flags**: `-Wall -Wextra -Werror -pedantic -pthread -fPIC`.
- **Formatting**: `clang-format` based on Google C/C++ style rules.

---

## 3. Development & Verification Workflow

### 3.1 Branching Strategy
- Branch from `main`.
- Use descriptive branch naming:
  - `feat/allocator-buddy-fallback`
  - `fix/proc-parser-zombie-state`
  - `perf/tui-refresh-rate`
  - `docs/clarify-canary-spec`

### 3.2 Code Formatting Pass
Before committing, enforce code formatting across all source, header, and test files:
```bash
# Check formatting
clang-format --dry-run --Werror src/**/*.c include/*.h tests/*.c

# Apply formatting in-place
clang-format -i src/**/*.c include/*.h tests/*.c
```

### 3.3 Full Verification Pipeline
Every pull request must pass the four-tier verification pipeline:

```bash
# 1. Clean build
make clean && make all

# 2. Run Unity unit tests and integration tests
make test

# 3. Verify under AddressSanitizer (ASan) & UBSan
make clean && make asan
./test_allocator
./test_proc_parser
./test_signal_handler

# 4. Memory leak verification under Valgrind
make clean && make all
make valgrind

# 5. Performance benchmark vs glibc
make benchmark

# 6. Concurrency stress test
bash scripts/stress_test.sh
```

---

## 4. Writing Unit & Integration Tests

All unit tests are built using the embedded [Unity C Framework](https://github.com/ThrowTheSwitch/Unity).

- **Allocator Tests** (`tests/test_allocator.c`):
  - Add test cases targeting boundary conditions: zero-byte allocations, exact bin matches, large `mmap` allocations, coalescing correctness, and canary corruption detection.
  - Follow the Unity pattern:
    ```c
    void test_custom_allocation_pattern(void) {
        void *p = my_malloc(64);
        TEST_ASSERT_NOT_NULL(p);
        my_free(p);
    }
    ```
- **Process Parser Tests** (`tests/test_proc_parser.c`):
  - Verify parsing of `/proc/stat`, `/proc/meminfo`, and active process metrics without leaking file descriptors.
- **Signal Handler Tests** (`tests/test_signal_handler.c`):
  - Validate signal name-to-integer mapping and target dispatch error handling.

---

## 5. Commit Message Conventions

We enforce [Conventional Commits (v1.0.0)](https://www.conventionalcommits.org/):

| Type | Description | Example |
|---|---|---|
| `feat` | Introduces a new feature or capability | `feat(allocator): add block splitting remainder threshold` |
| `fix` | Fixes an identified bug or failure | `fix(tui): correct Vim navigation keybinding conflict` |
| `perf` | Improves performance without altering behavior | `perf(parser): eliminate redundant stack buffer copies` |
| `test` | Adds or enhances unit or integration tests | `test(signal): add validation for invalid signal names` |
| `docs` | Documentation or architecture updates | `docs(adr): document hybrid sbrk/mmap trade-offs` |
| `refactor` | Code restructuring without feature changes | `refactor(server): streamline JSON serializer buffer math` |
| `chore` | Tooling, Makefile, CI or dependency changes | `chore(ci): update Ubuntu runner image to 24.04` |

---

## 6. Pull Request Checklist

Before opening a pull request, verify:
- [ ] Code compiles with `gcc` and `clang` under `-Wall -Wextra -Werror -pedantic`.
- [ ] `clang-format --dry-run --Werror src/**/*.c include/*.h tests/*.c` passes with zero warnings.
- [ ] `make test` completes with 100% test assertions passing.
- [ ] `make valgrind` reports **zero leaks and zero errors**.
- [ ] No heap allocations added inside `proc_parser_read_*` functions.
- [ ] Canary words (`0xDEADBEEF` and `0xBEEFDEAD`) remain intact.
- [ ] Relevant Architecture Decision Records (ADRs) or runbooks updated if system interfaces changed.
