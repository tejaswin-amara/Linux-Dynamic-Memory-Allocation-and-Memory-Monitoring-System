#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include "allocator.h"

/*
 * e2e_alloc_helper: C test harness for executing allocator verification commands.
 * Returns 0 on success, non-zero on failure.
 */

static int run_in_child_expecting_abort(void (*child_fn)(void)) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        /* Run child logic */
        child_fn();
        _exit(0); /* If it didn't abort, exit 0 */
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
        return 0; /* Successfully aborted */
    }
    fprintf(stderr, "Child did not abort with SIGABRT (status=%d)\n", status);
    return 1;
}

static void trigger_double_free(void) {
    allocator_init();
    void *p = my_malloc(64);
    if (!p) _exit(2);
    my_free(p);
    my_free(p); /* Should trigger fatal_abort */
}

static void trigger_canary_header(void) {
    allocator_init();
    void *p = my_malloc(64);
    if (!p) _exit(2);
    block_header_t *hdr = (block_header_t *)((char *)p - sizeof(block_header_t));
    hdr->magic_header = 0x12345678;
    my_free(p); /* Should trigger fatal_abort */
}

static void trigger_canary_footer(void) {
    allocator_init();
    void *p = my_malloc(64);
    if (!p) _exit(2);
    block_header_t *hdr = (block_header_t *)((char *)p - sizeof(block_header_t));
    block_footer_t *ftr = (block_footer_t *)((char *)hdr + hdr->block_size - sizeof(block_footer_t));
    ftr->magic_footer = 0x87654321;
    my_free(p); /* Should trigger fatal_abort */
}

static void trigger_realloc_uaf(void) {
    allocator_init();
    void *p = my_malloc(64);
    if (!p) _exit(2);
    my_free(p);
    void *q = my_realloc(p, 128); /* Should trigger fatal_abort */
    (void)q;
}

static int test_alignments(void) {
    size_t test_aligns[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    size_t count = sizeof(test_aligns) / sizeof(test_aligns[0]);

    for (size_t i = 0; i < count; i++) {
        size_t align = test_aligns[i];
        void *p = NULL;
        int res = posix_memalign(&p, align, 256);
        if (res != 0 || !p) {
            fprintf(stderr, "posix_memalign failed for align=%zu (res=%d)\n", align, res);
            return 1;
        }
        if (((uintptr_t)p % align) != 0) {
            fprintf(stderr, "Pointer %p not aligned to %zu\n", p, align);
            free(p);
            return 1;
        }
        memset(p, 0xEE, 256);
        free(p);
    }
    return 0;
}

static int test_coalesce_bidirectional(void) {
    allocator_init();
    allocator_stats_t before = allocator_get_stats();

    void *p1 = my_malloc(64);
    void *p2 = my_malloc(64);
    void *p3 = my_malloc(64);

    if (!p1 || !p2 || !p3) {
        fprintf(stderr, "Allocation failed in coalesce test\n");
        return 1;
    }

    my_free(p1);
    my_free(p3);
    my_free(p2); /* Should coalesce p1, p2, p3 into one chunk */

    /* Now allocate a block larger than any single original chunk */
    void *large = my_malloc(192);
    if (!large) {
        fprintf(stderr, "Failed to allocate coalesced block\n");
        return 1;
    }

    allocator_stats_t after = allocator_get_stats();
    /* Should not require a new sbrk allocation */
    if (after.sbrk_allocations != before.sbrk_allocations + 3) {
        fprintf(stderr, "Coalescing did not reuse space properly (sbrk before=%zu, after=%zu)\n",
                before.sbrk_allocations, after.sbrk_allocations);
        my_free(large);
        return 1;
    }

    my_free(large);
    if (allocator_verify_integrity() != 0) {
        fprintf(stderr, "Integrity verification failed after coalescing\n");
        return 1;
    }
    return 0;
}

static int test_churn_stress(int iterations) {
    allocator_init();
    void *ptrs[64] = {0};

    for (int iter = 0; iter < iterations; iter++) {
        int idx = rand() % 64;
        if (ptrs[idx] == NULL) {
            size_t sz = 16 + (rand() % 4096);
            ptrs[idx] = my_malloc(sz);
            if (ptrs[idx]) {
                memset(ptrs[idx], (iter & 0xFF), sz);
            }
        } else {
            if (rand() % 3 == 0) {
                /* realloc */
                size_t newsz = 16 + (rand() % 8192);
                void *np = my_realloc(ptrs[idx], newsz);
                if (np) {
                    ptrs[idx] = np;
                }
            } else {
                /* free */
                my_free(ptrs[idx]);
                ptrs[idx] = NULL;
            }
        }
        if (iter % 200 == 0) {
            if (allocator_verify_integrity() != 0) {
                fprintf(stderr, "Integrity check failed at iteration %d\n", iter);
                return 1;
            }
        }
    }

    for (int i = 0; i < 64; i++) {
        if (ptrs[i]) {
            my_free(ptrs[i]);
            ptrs[i] = NULL;
        }
    }

    return allocator_verify_integrity() == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <alignments|double_free|canary_header|canary_footer|realloc_uaf|coalesce|churn> [args]\n", argv[0]);
        return 2;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "alignments") == 0) {
        return test_alignments();
    } else if (strcmp(cmd, "double_free") == 0) {
        return run_in_child_expecting_abort(trigger_double_free);
    } else if (strcmp(cmd, "canary_header") == 0) {
        return run_in_child_expecting_abort(trigger_canary_header);
    } else if (strcmp(cmd, "canary_footer") == 0) {
        return run_in_child_expecting_abort(trigger_canary_footer);
    } else if (strcmp(cmd, "realloc_uaf") == 0) {
        return run_in_child_expecting_abort(trigger_realloc_uaf);
    } else if (strcmp(cmd, "coalesce") == 0) {
        return test_coalesce_bidirectional();
    } else if (strcmp(cmd, "churn") == 0) {
        int iters = (argc > 2) ? atoi(argv[2]) : 1000;
        return test_churn_stress(iters);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    return 2;
}
