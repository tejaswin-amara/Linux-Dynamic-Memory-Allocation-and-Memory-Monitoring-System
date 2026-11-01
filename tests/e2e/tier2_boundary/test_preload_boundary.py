"""
Tier 2: Boundary & Corner Cases - Interposition & Bootstrap Arena (LD_PRELOAD)
Covers free(NULL) under preload, reallocarray overflow rejection, cross-thread freeing, rapid churn, and isolation.
"""

import os
import unittest
from tests.e2e.e2e_common import compile_c_code, run_under_preload, BIN_DIR


class TestPreloadBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.makedirs(BIN_DIR, exist_ok=True)
        src = """
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *g_cross_ptr = NULL;

static void *thread_alloc(void *arg) {
    (void)arg;
    g_cross_ptr = malloc(128);
    if (g_cross_ptr) memset(g_cross_ptr, 0x77, 128);
    return NULL;
}

static void *thread_free(void *arg) {
    (void)arg;
    if (g_cross_ptr) {
        free(g_cross_ptr);
        g_cross_ptr = NULL;
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *test = argv[1];

    if (strcmp(test, "free_null") == 0) {
        free(NULL);
        return 0;
    } else if (strcmp(test, "reallocarray_overflow") == 0) {
        errno = 0;
        void *p = reallocarray(NULL, (size_t)1 << 32, (size_t)1 << 32);
        if (p != NULL) { free(p); return 2; }
        if (errno != ENOMEM) return 3;
        return 0;
    } else if (strcmp(test, "cross_thread_free") == 0) {
        pthread_t t1, t2;
        if (pthread_create(&t1, NULL, thread_alloc, NULL) != 0) return 4;
        pthread_join(t1, NULL);
        if (!g_cross_ptr) return 5;
        if (pthread_create(&t2, NULL, thread_free, NULL) != 0) return 6;
        pthread_join(t2, NULL);
        return 0;
    } else if (strcmp(test, "smoke_iteration") == 0) {
        void *p = malloc(64);
        if (!p) return 7;
        memset(p, 0x11, 64);
        free(p);
        return 0;
    }
    return 99;
}
"""
        cls.bin_path = os.path.join(BIN_DIR, "e2e_test_preload_tier2")
        compile_c_code(src, cls.bin_path)

    def test_01_preload_free_null(self):
        """free(NULL) under LD_PRELOAD interposition executes cleanly without crash."""
        code, out, err = run_under_preload([self.bin_path, "free_null"])
        self.assertEqual(code, 0, f"free(NULL) under preload failed: {err}")

    def test_02_preload_reallocarray_integer_overflow_rejection(self):
        """reallocarray detects multiplication overflow, returns NULL, and sets errno = ENOMEM."""
        code, out, err = run_under_preload([self.bin_path, "reallocarray_overflow"])
        self.assertEqual(code, 0, f"reallocarray overflow rejection failed (exit={code}): {err}")

    def test_03_preload_cross_thread_free(self):
        """Cross-thread deallocation (allocated on thread 1, freed on thread 2) succeeds without abort."""
        code, out, err = run_under_preload([self.bin_path, "cross_thread_free"])
        self.assertEqual(code, 0, f"Cross-thread free under LD_PRELOAD failed: {err}")

    def test_04_preload_rapid_process_churn_no_leak(self):
        """15 rapid consecutive subprocess invocations under LD_PRELOAD succeed cleanly."""
        for _ in range(15):
            code, out, err = run_under_preload([self.bin_path, "smoke_iteration"])
            self.assertEqual(code, 0, f"Subprocess churn failed under preload: {err}")

    def test_05_preload_environment_isolation(self):
        """Standard utilities run with preload environment while retaining output validity."""
        code, out, err = run_under_preload(["/bin/uname", "-s"])
        self.assertEqual(code, 0)
        self.assertIn("Linux", out)


if __name__ == "__main__":
    unittest.main()
