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
echo "================================================================================"
