# Runbook: Debugging Memory Leaks & Allocator Diagnostics

## Purpose
This runbook details procedures for diagnosing heap corruption, double-free bugs, and memory leaks when using `libmyalloc.so`.

## Tools and Diagnostic Flags

### 1. Magic Canary Verification
- Every allocated block is enclosed by `0xDEADBEEF`.
- If an application overwrites buffer boundaries, `free()` or `realloc()` asserts failure:
  `[ALLOCATOR ERROR] Canary corruption detected at block 0x... Expected 0xDEADBEEF, found 0x...`

### 2. Valgrind Integration
Execute binaries under Valgrind with `libmyalloc.so`:
```bash
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./test_allocator
```

### 3. AddressSanitizer (ASan) & UBSan Build
Compile and execute the sanitizer instrumented binary:
```bash
make clean
make asan
./test_allocator
```

### 4. LD_PRELOAD Tracing
Trace external commands to identify leak origins:
```bash
LD_PRELOAD=./libmyalloc.so /bin/ls -la
```
