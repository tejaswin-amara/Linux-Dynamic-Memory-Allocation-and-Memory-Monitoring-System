# ADR-001: Segregated Free Lists and Hybrid sbrk/mmap Allocation Architecture

## Status
Accepted

## Context
KLEF 25CS2104E (Course Outcome CO4) requires implementing a custom memory allocator capable of replacing standard `glibc` `malloc`, `free`, `realloc`, and `calloc`.
The allocator must strike an optimal balance between low metadata overhead, fast allocation lookup ($O(1)$ to $O(k)$ within segregated bins), minimal heap fragmentation, and dynamic scaling under varying workload sizes.

## Decision
1. **Hybrid Heap Growth Mechanism**:
   - **Small & Medium Allocations (< 128 KB)**: Allocate via `sbrk()`, advancing the program break point. These blocks are coalesced and stored in a segregated doubly linked free list.
   - **Large Allocations (>= 128 KB)**: Allocate directly via `mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`. Upon `free()`, memory is directly unmapped using `munmap()`, avoiding virtual address space pollution.
2. **Segregated Free Lists**:
   - 10 segregated size classes (powers of two and sub-page bins: 16B, 32B, 64B, 128B, 256B, 512B, 1024B, 2048B, 4096B, and general large).
   - Best-fit search within the target bin; escalates to higher bins on miss.
3. **Integrity & Boundary Tags**:
   - Every block header contains a magic canary (`0xDEADBEEF`), payload size, total size, and status flags (`is_free`, `is_mmap`).
   - Bidirectional pointers (`next`, `prev`) allow immediate $O(1)$ removal and coalescing with adjacent free chunks.
4. **Concurrency**:
   - Heap access is guarded by a global recursive/fast POSIX mutex (`pthread_mutex_t`), ensuring thread safety under concurrent allocations.

## Consequences
- Predictable allocation time with reduced fragmentation compared to a naive first-fit linked list.
- Canary validation provides instant detection of heap corruption or double-free errors.
- Small memory overhead for boundary tags (32 bytes per block) compensated by 8-byte/16-byte alignment guarantees.
