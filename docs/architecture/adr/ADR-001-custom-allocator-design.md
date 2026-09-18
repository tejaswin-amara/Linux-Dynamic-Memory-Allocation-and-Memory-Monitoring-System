# ADR-001: Segregated Free Lists and Hybrid sbrk/mmap Allocation Architecture

## Status
**Accepted** (Implemented in `libmyalloc.so`)

## Context & Problem Statement
In systems software development and operating systems curricula (KLEF 25CS2104E, Course Outcome CO4), memory allocation is the foundational layer upon which user applications execute. Standard runtime memory allocators (such as `glibc` `ptmalloc3`) are highly complex, multi-arena systems designed to maximize general-purpose throughput across hundreds of threads.

For our high-performance systems engineering capstone, we required a custom dynamic memory allocator capable of serving as a drop-in replacement for `malloc`, `free`, `calloc`, and `realloc` via `LD_PRELOAD`. The system must:
1. Guarantee $O(1)$ to $O(k)$ allocation lookup latency for typical small-to-medium objects.
2. Minimize internal and external heap fragmentation under continuous, randomized allocation and deallocation cycles.
3. Eliminate memory leaks and provide immediate diagnostic detection for heap corruptions, buffer overruns, and double-free bugs.
4. Support clean page return to the operating system for large allocation footprints.
5. Provide strict 16-byte payload alignment conforming to x86_64 ABI requirements.

## Decision Drivers
- **Lookup Performance**: Linear linked list traversal ($O(N)$) introduces intolerable latency under heavy allocation workloads (e.g. 500,000 ops).
- **Fragmentation Resilience**: Naive first-fit allocators degrade rapidly over time as free holes fragment the heap.
- **Memory Safety Hardening**: C applications are prone to off-by-one overwrites and double-free calls; the allocator must detect these anomalies proactively.
- **Kernel Resource Efficiency**: Releasing memory back to the kernel via `sbrk()` is constrained by top-of-heap boundaries; large allocations must not pin smaller allocations in the heap.

## Considered Alternatives
1. **Single Doubly-Linked Free List (First-Fit / Best-Fit)**:
   - *Pros*: Simple implementation.
   - *Cons*: Worst-case $O(N)$ search time; high fragmentation as list grows; poor locality of reference.
2. **Binary Buddy Allocator**:
   - *Pros*: Rapid power-of-two coalescing and splitting; predictable metadata.
   - *Cons*: Severe internal fragmentation (up to 50% for allocations just over power-of-two boundaries); high metadata overhead for sub-page objects.
3. **Fixed Slab / Cache Allocator**:
   - *Pros*: Near-zero fragmentation for uniform kernel objects.
   - *Cons*: Inflexible for arbitrary user-space allocation requests of variable lengths.
4. **Segregated Free Lists with Hybrid `sbrk`/`mmap` and Boundary Tags (Selected)**:
   - *Pros*: Fast lookup within discrete size bins, bounded search time, low fragmentation via Best-Fit within bins, immediate $O(1)$ bidirectional coalescing via boundary tags, and direct OS paging for large blocks.

## Decision Outcome
We decided to implement a **10-bin Segregated Free List allocator with Hybrid `sbrk`/`mmap` growth and boundary-tag canaries**:

### 1. Hybrid Allocation Mechanism
- **Small & Medium Chunks (`< 128 KB`)**: Allocated via `sbrk(2)` advancing the process program break. Free chunks are organized in segregated doubly-linked lists.
- **Large Chunks (`≥ 128 KB`)**: Allocated directly from the kernel using anonymous memory maps via `mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`. Upon `my_free()`, memory is immediately unmapped using `munmap()`, preventing virtual memory fragmentation.

### 2. Ten Discrete Segregated Size Classes
The segregated bins partition chunks according to the following thresholds:
- Class 0: $\le 32\text{ B}$
- Class 1: $\le 64\text{ B}$
- Class 2: $\le 128\text{ B}$
- Class 3: $\le 256\text{ B}$
- Class 4: $\le 512\text{ B}$
- Class 5: $\le 1024\text{ B (1 KB)}$
- Class 6: $\le 2048\text{ B (2 KB)}$
- Class 7: $\le 4096\text{ B (4 KB)}$
- Class 8: $\le 8192\text{ B (8 KB)}$
- Class 9: $> 8192\text{ B} \dots < 128\text{ KB}$

Within each bin, a **Best-Fit** search is conducted. If no suitable block is found in the designated bin, the allocator escalates to higher bins before requesting fresh heap memory via `sbrk()`.

### 3. Boundary Tags and Canary Verification
Each chunk embeds metadata before and after the payload:
- **Block Header (32 Bytes)**:
  - `magic_header = 0xDEADBEEF`: Canary detecting underflow/corruption.
  - `is_free`: 1 if free, 0 if allocated.
  - `requested_size`: Exact bytes requested by caller.
  - `block_size`: Total chunk size (including header, footer, and padding).
  - `is_mmap`: Flag distinguishing heap vs anonymous mapped pages.
  - `*next`, `*prev`: Doubly linked pointers for $O(1)$ removal and insertion.
- **Block Footer (16 Bytes)**:
  - `magic_footer = 0xBEEFDEAD`: Trailing canary detecting overflow.
  - `block_size`: Total block size matching the header, facilitating backward coalescing.

### 4. Bidirectional $O(1)$ Coalescing
Upon `my_free()`, the allocator validates both canaries. It checks the forward adjacent block (`header + block_size`) and backward adjacent block (`header - prev_footer->block_size`). If adjacent blocks are marked free and non-mmap, they are unlinked from their segregated lists and merged into a larger contiguous block.

### 5. Concurrency Control
All heap operations are guarded by a global mutex (`pthread_mutex_t alloc_mutex`), ensuring thread safety for concurrent multi-threaded applications.

## Consequences

### Positive
- **Predictable Latency**: Bin partitioning isolates search to small pools of similarly sized blocks.
- **Active Safety**: Corrupted canaries halt anomalous execution before heap structures are compromised.
- **Zero OS Memory Waste**: Large blocks immediately yield virtual and physical memory back to the OS via `munmap()`.
- **Drop-in Preload**: Compatible with unmodified Linux binaries (`LD_PRELOAD=./libmyalloc.so`).

### Negative
- **Metadata Overhead**: 48 bytes of metadata (32B header + 16B footer) per block. For micro-allocations (e.g. 16B), metadata overhead is significant; however, this is acceptable for systems software and pedagogical verification.
- **Coarse Locking**: Global mutex serializes concurrent allocations across threads; suitable for desktop/monitoring systems, though multi-arena partitioning would be needed for massive 64-core workloads.

## Verification & Compliance
- **Unit Tests**: Full suite in `tests/test_allocator.c` covering allocation, reallocation, calloc zero-initialization, coalescing, and canary validation.
- **Sanitizers**: Verified with ASan & UBSan (`make asan`).
- **Valgrind**: Verified zero memory leaks under `make valgrind`.
- **Benchmarking**: Validated against `glibc` `malloc` at ~9.5M ops/sec with stable fragmentation behavior (`make benchmark`).
