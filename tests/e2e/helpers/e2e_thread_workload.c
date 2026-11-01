#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_THREADS 4
#define OPS_PER_THREAD 1000

typedef struct {
    int thread_id;
    int success_count;
    int failure_count;
} thread_arg_t;

static void *worker_func(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    void *ptrs[16] = {0};
    size_t sizes[16] = {0};

    for (int i = 0; i < OPS_PER_THREAD; i++) {
        int slot = rand() % 16;
        if (ptrs[slot] == NULL) {
            size_t sz = 32 + (rand() % 1024);
            ptrs[slot] = malloc(sz);
            if (ptrs[slot]) {
                sizes[slot] = sz;
                memset(ptrs[slot], (targ->thread_id + i) & 0xFF, sz);
                targ->success_count++;
            } else {
                targ->failure_count++;
            }
        } else {
            if (rand() % 2 == 0) {
                size_t newsz = sizes[slot] * 2 + 16;
                void *np = realloc(ptrs[slot], newsz);
                if (np) {
                    ptrs[slot] = np;
                    sizes[slot] = newsz;
                    targ->success_count++;
                } else {
                    targ->failure_count++;
                }
            } else {
                free(ptrs[slot]);
                ptrs[slot] = NULL;
                sizes[slot] = 0;
                targ->success_count++;
            }
        }
    }

    for (int j = 0; j < 16; j++) {
        if (ptrs[j]) {
            free(ptrs[j]);
            ptrs[j] = NULL;
        }
    }
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].success_count = 0;
        args[i].failure_count = 0;
        if (pthread_create(&threads[i], NULL, worker_func, &args[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    int total_failures = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_failures += args[i].failure_count;
    }

    if (total_failures > 0) {
        fprintf(stderr, "Encountered %d allocation failures across threads\n", total_failures);
        return 1;
    }

    return 0;
}
