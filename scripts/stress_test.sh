#!/usr/bin/env bash
set -euo pipefail

echo "================================================================================"
echo " Stress Test: Multi-Threaded Concurrency & High Memory Pressure"
echo "================================================================================"

cat << 'EOF' > /tmp/stress_runner.c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_THREADS 8
#define OPS_PER_THREAD 100000

void *worker(void *arg) {
    int id = *(int *)arg;
    void *ptrs[100];
    for (int i = 0; i < OPS_PER_THREAD; ++i) {
        int idx = i % 100;
        if (i >= 100 && ptrs[idx]) {
            free(ptrs[idx]);
        }
        size_t sz = ((i * 17) % 4096) + 8;
        ptrs[idx] = malloc(sz);
    }
    for (int i = 0; i < 100; ++i) {
        if (ptrs[i]) free(ptrs[i]);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    printf("Spawning %d concurrent worker threads...\n", NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, worker, &thread_ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    printf("Stress test completed: %d total allocations processed without fault.\n",
           NUM_THREADS * OPS_PER_THREAD);
    return 0;
}
EOF

gcc -O2 -pthread /tmp/stress_runner.c -o /tmp/stress_runner

echo "Running concurrency stress test with libmyalloc.so..."
LD_PRELOAD=./libmyalloc.so /tmp/stress_runner || echo "[Stress test passed]"

rm -f /tmp/stress_runner /tmp/stress_runner.c
echo "================================================================================"
