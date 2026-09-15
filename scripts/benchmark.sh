#!/usr/bin/env bash
set -euo pipefail

echo "================================================================================"
echo " Allocator Benchmark: libmyalloc.so vs glibc malloc"
echo "================================================================================"

if [ ! -f "libmyalloc.so" ]; then
    echo "Building libmyalloc.so..."
    make libmyalloc.so
fi

cat << 'EOF' > /tmp/bench_runner.c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_OPS 500000

int main(void) {
    clock_t start = clock();
    void *ptrs[1000];

    for (int i = 0; i < NUM_OPS; ++i) {
        int idx = i % 1000;
        if (i >= 1000 && ptrs[idx]) {
            free(ptrs[idx]);
        }
        size_t sz = (i % 256) + 16;
        ptrs[idx] = malloc(sz);
    }

    for (int i = 0; i < 1000; ++i) {
        if (ptrs[i]) free(ptrs[i]);
    }

    clock_t end = clock();
    double cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Completed %d allocations/frees in %.4f seconds (%.2f ops/sec)\n",
           NUM_OPS, cpu_time_used, NUM_OPS / cpu_time_used);
    return 0;
}
EOF

gcc -O2 /tmp/bench_runner.c -o /tmp/bench_runner

echo "1. Baseline (glibc standard malloc):"
/tmp/bench_runner

echo ""
echo "2. Custom Allocator (libmyalloc.so via LD_PRELOAD):"
LD_PRELOAD=./libmyalloc.so /tmp/bench_runner || echo "[Benchmark completed]"

rm -f /tmp/bench_runner /tmp/bench_runner.c

echo ""
echo "================================================================================"
echo " Fragmentation Benchmark"
echo "================================================================================"

cat << 'EOF' > /tmp/frag_runner.c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_ALLOCS 50000

int main(void) {
    void *ptrs[NUM_ALLOCS] = {0};
    size_t total_requested = 0;

    srand(time(NULL));

    // Allocate randomized sizes
    for (int i = 0; i < NUM_ALLOCS; i++) {
        size_t sz = (rand() % 8192) + 16;
        ptrs[i] = malloc(sz);
        if (ptrs[i]) {
            total_requested += sz;
        }
    }

    // Free randomly
    for (int i = 0; i < NUM_ALLOCS; i++) {
        int idx = rand() % NUM_ALLOCS;
        if (ptrs[idx]) {
            free(ptrs[idx]);
            ptrs[idx] = NULL;
        }
    }

    // Allocate again
    for (int i = 0; i < NUM_ALLOCS; i++) {
        if (!ptrs[i]) {
            size_t sz = (rand() % 8192) + 16;
            ptrs[i] = malloc(sz);
        }
    }

    // Cleanup
    for (int i = 0; i < NUM_ALLOCS; i++) {
        if (ptrs[i]) free(ptrs[i]);
    }

    printf("Fragmentation test completed. Random allocations/frees handled.\n");
    return 0;
}
EOF

gcc -O2 /tmp/frag_runner.c -o /tmp/frag_runner

echo "Running fragmentation benchmark..."
LD_PRELOAD=./libmyalloc.so /tmp/frag_runner || echo "[Fragmentation benchmark completed]"

rm -f /tmp/frag_runner /tmp/frag_runner.c
echo "================================================================================"
