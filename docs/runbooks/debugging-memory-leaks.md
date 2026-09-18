# Runbook: Debugging Memory Leaks & Allocator Diagnostics

## Purpose & Scope
This operational runbook provides engineers and developers with comprehensive forensic workflows for diagnosing heap corruption, memory leaks, buffer overflows, and double-free bugs within the **`libmyalloc.so`** custom allocator and external applications running under `LD_PRELOAD`.

---

## 1. Allocator Canary Forensics

`libmyalloc.so` encloses every allocated chunk with two distinct 32-bit magic canaries:
- **Header Canary (`0xDEADBEEF`)**: Positioned at offset 0 of `block_header_t` immediately before the user payload.
- **Footer Canary (`0xBEEFDEAD`)**: Positioned at offset 0 of `block_footer_t` immediately following the user payload.

### Forensic Triage Table
| Log Output | Failure Type | Root Cause Analysis |
|---|---|---|
| `[ERROR] Heap corruption: Invalid magic header canary at <PTR>` | **Buffer Underflow** or **Wild Pointer Write** | An application wrote preceding the allocated block boundary (e.g. `ptr[-1] = 0`), corrupting `magic_header`. |
| `[ERROR] Heap corruption: Invalid magic footer canary at <PTR>` | **Buffer Overflow** or **Off-by-One Write** | An application wrote past the allocated block length (e.g. `ptr[size] = 0`), overwriting `magic_footer`. |
| `[ERROR] Heap canary corruption detected in free list at <PTR>` | **Use-After-Free** | A block already returned to the free list had its payload or boundary tags overwritten while residing in a segregated bin. |
| `[ERROR] munmap syscall failed at <PTR>` | **Unmapped Address Invalidation** | Attempted to unmap an address range not created via anonymous `mmap` or with corrupted size metadata. |

---

## 2. Valgrind Memcheck Leak Verification

Valgrind provides bit-level tracking of defined memory and detects leaks upon program exit.

### Standard Leak Check Command
```bash
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --error-exitcode=1 \
         ./test_allocator
```

### Interpreting Valgrind Report Categories
- **`definitely lost`**: Heap blocks allocated where no pointer to the block exists at program exit. **Must be 0 bytes.**
- **`indirectly lost`**: Heap blocks that are only reachable via pointers in definitely lost blocks. **Must be 0 bytes.**
- **`possibly lost`**: Pointers that point into the interior of a block rather than the start (common with interior pointers or misaligned structs).
- **`still reachable`**: Pointers still maintained by global tables or static variables at normal exit. `libmyalloc.so` cleans up all segregated heads on `allocator_destroy()`.

---

## 3. AddressSanitizer (ASan) & UBSan Diagnostics

AddressSanitizer instruments memory operations with shadow memory checks to catch invalid accesses at runtime with zero false positives.

### Compiling and Running with Sanitizers
```bash
# Clean existing artifacts
make clean

# Compile with ASan & UBSan enabled (-fsanitize=address,undefined)
make asan

# Run test targets under sanitizer instrumentation
./test_allocator
./test_proc_parser
./test_signal_handler
```

### Interpreting Sanitizer Reports
When a fault occurs, ASan outputs a structured crash dump:
1. **Crash Cause**: e.g., `heap-buffer-overflow`, `heap-use-after-free`, or `double-free`.
2. **Access Details**: Read/Write size, address offset relative to allocated block.
3. **Stack Trace**: Exact source line of the illegal memory access.
4. **Allocation Stack Trace**: The exact code line where `my_malloc()` originally allocated the chunk.

---

## 4. GDB In-Flight Heap Inspection

When debugging a crashed binary or core dump, inspect allocator structures directly in GDB:

### 1. Launching GDB with Target Binary
```bash
gdb ./mem_monitor
(gdb) break my_malloc
(gdb) break my_free
(gdb) run
```

### 2. Inspecting Block Header and Canaries
```gdb
# Given a user pointer $rax / ptr:
(gdb) set $header = (block_header_t *)((char *)ptr - sizeof(block_header_t))
(gdb) print *$header

# Expected output:
# $1 = {magic_header = 3735928559, is_free = 0, requested_size = 64, block_size = 112, is_mmap = 0, padding = 0, next = 0x0, prev = 0x0}
# Note: 3735928559 == 0xDEADBEEF in decimal

# Check footer canary:
(gdb) set $footer = (block_footer_t *)((char *)$header + $header->block_size - sizeof(block_footer_t))
(gdb) print/x $footer->magic_footer
# Expected: 0xBEEFDEAD
```

### 3. Walking Segregated Free Lists
```gdb
# Inspect bin 2 (blocks <= 128 bytes):
(gdb) print segregated_heads[2]
(gdb) print *segregated_heads[2]
```

---

## 5. Tracing External Binaries with `LD_PRELOAD`

To isolate memory anomalies in external Linux applications executed with `libmyalloc.so`:

```bash
# 1. Trace syscalls relating to heap growth and memory mappings:
LD_PRELOAD=./libmyalloc.so strace -e trace=brk,mmap,munmap ls -la /tmp

# 2. Trace dynamic symbol resolution:
LD_DEBUG=bindings LD_PRELOAD=./libmyalloc.so ls 2>&1 | grep "my_malloc"

# 3. Catch segmentation faults with core dump creation:
ulimit -c unlimited
LD_PRELOAD=./libmyalloc.so /path/to/flaky_app
gdb /path/to/flaky_app core -ex "bt" -ex "quit"
```
