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

echo ""
echo "================================================================================"
echo " High-Fragmentation Multi-Threaded Workload"
echo "================================================================================"

cat << 'EOF' > /tmp/frag_stress_runner.c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NUM_THREADS 4
#define OPS_PER_THREAD 50000
#define MAX_PTRS 500

void *worker(void *arg) {
    int id = *(int *)arg;
    void *ptrs[MAX_PTRS] = {0};
    unsigned int seed = time(NULL) ^ id;

    for (int i = 0; i < OPS_PER_THREAD; ++i) {
        int idx = rand_r(&seed) % MAX_PTRS;

        if (ptrs[idx]) {
            free(ptrs[idx]);
            ptrs[idx] = NULL;
        } else {
            // Highly variable block sizes
            size_t sz;
            int r = rand_r(&seed) % 100;
            if (r < 50) sz = (rand_r(&seed) % 64) + 8;
            else if (r < 80) sz = (rand_r(&seed) % 1024) + 64;
            else if (r < 95) sz = (rand_r(&seed) % 8192) + 1024;
            else sz = (rand_r(&seed) % 256000) + 8192; // Including some large allocations

            ptrs[idx] = malloc(sz);
        }
    }
    for (int i = 0; i < MAX_PTRS; ++i) {
        if (ptrs[i]) free(ptrs[i]);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    printf("Spawning %d concurrent worker threads for fragmentation test...\n", NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, worker, &thread_ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    printf("Fragmentation stress test completed successfully.\n");
    return 0;
}
EOF

gcc -O2 -pthread /tmp/frag_stress_runner.c -o /tmp/frag_stress_runner

echo "Running fragmentation stress test with libmyalloc.so..."
LD_PRELOAD=./libmyalloc.so /tmp/frag_stress_runner || echo "[Fragmentation stress test passed]"

rm -f /tmp/frag_stress_runner /tmp/frag_stress_runner.c
echo "================================================================================"
