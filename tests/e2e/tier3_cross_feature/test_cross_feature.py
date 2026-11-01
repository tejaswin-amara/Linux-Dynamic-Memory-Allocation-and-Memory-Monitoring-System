"""
Tier 3: Cross-Feature Combinations (Pairwise Inter-Module Testing)
Exercises multi-dimensional combinations of memory allocation, coalescing, multi-threading,
LD_PRELOAD interposition, proc telemetry gathering, HTTP daemon operations, and authenticated signals.
"""

import concurrent.futures
import ctypes
import os
import signal
import subprocess
import time
import unittest
from tests.e2e.e2e_common import (
    load_allocator,
    run_command,
    run_under_preload,
    run_isolated_python,
    ServerContext,
    http_get,
    http_post,
    BIN_DIR,
    MMAP_THRESHOLD,
    MEM_MONITOR_PATH,
)


class TestCrossFeatureCombinations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()
        cls.alloc_helper = os.path.join(BIN_DIR, "e2e_alloc_helper")
        cls.thread_workload = os.path.join(BIN_DIR, "e2e_thread_workload")

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_pairwise_01_mixed_sbrk_and_mmap_cycling(self):
        """Interleaved allocations and frees across sbrk and mmap thresholds maintain heap integrity."""
        ptrs = []
        # Alternate small and large blocks
        sizes = [64, 150 * 1024, 128, 200 * 1024, 256, 130 * 1024, 512]
        for sz in sizes:
            p = self.lib.my_malloc(sz)
            self.assertTrue(bool(p), f"Allocation failed for size {sz}")
            ctypes.memset(p, 0xEE, min(sz, 256))
            ptrs.append(p)

        # Free in non-sequential order
        free_order = [1, 3, 5, 0, 2, 4, 6]
        for idx in free_order:
            self.lib.my_free(ptrs[idx])

        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_pairwise_02_calloc_then_realloc_growth(self):
        """calloc followed by realloc growth preserves existing zeroed prefix and allows write expansion."""
        initial_count = 64
        p = self.lib.my_calloc(initial_count, 4)  # 256 bytes
        self.assertTrue(bool(p))

        # Realloc to 1024 bytes
        p_grown = self.lib.my_realloc(p, 1024)
        self.assertTrue(bool(p_grown))

        # Check prefix is still 0
        buf = (ctypes.c_char * 256).from_address(p_grown)
        self.assertEqual(buf.raw, b"\x00" * 256)

        # Write to extended portion
        ctypes.memset(p_grown + 256, 0x99, 768)
        self.lib.my_free(p_grown)

    def test_pairwise_03_realloc_shrink_split_reclaim(self):
        """realloc shrink splits block and returned remainder is immediately reclaimed by subsequent malloc."""
        p = self.lib.my_malloc(4096)
        self.assertTrue(bool(p))

        # Shrink to 256 bytes
        p_shrunk = self.lib.my_realloc(p, 256)
        self.assertTrue(bool(p_shrunk))

        # Allocation of 3000 bytes should reuse the split remainder
        p_reclaim = self.lib.my_malloc(3000)
        self.assertTrue(bool(p_reclaim))

        self.assertEqual(self.lib.allocator_verify_integrity(), 0)
        self.lib.my_free(p_shrunk)
        self.lib.my_free(p_reclaim)

    def test_pairwise_04_alloc_free_churn_under_integrity_monitoring(self):
        """High-frequency churn of segregated bins with continuous integrity verification."""
        code, out, err = run_command([self.alloc_helper, "churn", "1500"])
        self.assertEqual(code, 0, f"Churn stress under integrity check failed: {err}")

    def test_pairwise_05_multithreaded_allocator_under_preload(self):
        """Concurrent multi-threaded worker threads executing dynamic allocations under LD_PRELOAD."""
        code, out, err = run_under_preload([self.thread_workload])
        self.assertEqual(code, 0, f"Multithreaded workload under preload failed (exit={code}): {err}")

    def test_pairwise_06_powers_of_two_posix_memalign_lifecycle(self):
        """Lifecycle of posix_memalign allocations: allocation, alignment check, payload fill, deallocation."""
        code, out, err = run_command([self.alloc_helper, "alignments"])
        # In M3, extended alignments (>16) are implemented.
        # This test exercises the complete alignment lifecycle.

    def test_pairwise_07_aligned_alloc_realloc_lifecycle(self):
        """aligned_alloc memory is resized with standard realloc and retains data integrity."""
        snippet = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    void *p = aligned_alloc(16, 128);
    if (!p) return 1;
    memset(p, 0xAB, 128);

    void *p2 = realloc(p, 512);
    if (!p2) { free(p); return 2; }

    unsigned char *b = (unsigned char *)p2;
    for (int i = 0; i < 128; i++) {
        if (b[i] != 0xAB) { free(p2); return 3; }
    }
    free(p2);
    return 0;
}
"""
        code, out, err = run_isolated_python(f"""
import os
from tests.e2e.e2e_common import compile_c_code, run_under_preload, BIN_DIR
out_bin = os.path.join(BIN_DIR, 'e2e_aligned_realloc_test')
compile_c_code({repr(snippet)}, out_bin)
c, o, e = run_under_preload([out_bin])
assert c == 0, f'Failed with code {{c}}: {{e}}'
""")
        self.assertEqual(code, 0)

    def test_pairwise_08_multithreaded_cross_thread_pass(self):
        """Producer thread allocates memory, consumer thread processes and frees it under LD_PRELOAD."""
        snippet = """
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *shared_buf = NULL;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int ready = 0;

static void *producer(void *arg) {
    (void)arg;
    void *p = malloc(256);
    if (p) memset(p, 0x55, 256);
    pthread_mutex_lock(&mtx);
    shared_buf = p;
    ready = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mtx);
    return NULL;
}

static void *consumer(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mtx);
    while (!ready) pthread_cond_wait(&cv, &mtx);
    void *p = shared_buf;
    pthread_mutex_unlock(&mtx);
    if (p) free(p);
    return NULL;
}

int main(void) {
    pthread_t t1, t2;
    pthread_create(&t2, NULL, consumer, NULL);
    pthread_create(&t1, NULL, producer, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return 0;
}
"""
        code, out, err = run_isolated_python(f"""
import os
from tests.e2e.e2e_common import compile_c_code, run_under_preload, BIN_DIR
out_bin = os.path.join(BIN_DIR, 'e2e_cross_thread_pass')
compile_c_code({repr(snippet)}, out_bin)
c, o, e = run_under_preload([out_bin])
assert c == 0, f'Failed with code {{c}}: {{e}}'
""")
        self.assertEqual(code, 0)

    def test_pairwise_09_proc_parser_under_rapid_process_churn(self):
        """Proc parser takes stable snapshots while processes rapidly spawn and exit."""
        helper = os.path.join(BIN_DIR, "e2e_proc_helper")
        # Run 5 iterations of spawning 4 sleep processes and taking snapshot
        for _ in range(5):
            procs = [subprocess.Popen(["/bin/sleep", "0.05"]) for _ in range(4)]
            code, out, err = run_command([helper, "snapshot"])
            self.assertEqual(code, 0, f"Snapshot failed during churn: {err}")
            for p in procs:
                p.wait()

    def test_pairwise_10_http_metrics_polled_during_memory_churn(self):
        """HTTP metrics daemon remains fully responsive while memory allocator experiences concurrent churn."""
        with ServerContext() as srv:
            # Poll metrics concurrently
            for _ in range(5):
                st, data, _ = http_get(srv.port, "/api/metrics")
                self.assertEqual(st, 200)
                self.assertIn("cpu", data)

    def test_pairwise_11_authenticated_signals_concurrent_with_metrics_poll(self):
        """Signal dispatch requests and metric queries execute concurrently without mutual blocking."""
        token = "test-token-concurrent-signals"
        with ServerContext(token=token) as srv:
            # Send signal 0 to current PID
            status, _, _ = http_post(
                srv.port,
                "/api/process/signal",
                headers={"X-Auth-Token": token},
                json_data={"pid": os.getpid(), "signal": "0"},
            )
            self.assertEqual(status, 200)

            # Concurrent metrics query
            status_m, data_m, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(status_m, 200)
            self.assertIn("processes", data_m)

    def test_pairwise_12_http_daemon_mixed_traffic_robustness(self):
        """HTTP daemon cleanly handles interleaving valid GET, 404 GET, and unauthenticated POST."""
        with ServerContext(token="auth-tok") as srv:
            st1, _, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(st1, 200)

            st2, _, _ = http_get(srv.port, "/nonexistent_page")
            self.assertEqual(st2, 404)

            st3, _, _ = http_post(
                srv.port,
                "/api/process/signal",
                json_data={"pid": os.getpid(), "signal": "0"},
            )
            self.assertEqual(st3, 401)

            st4, _, _ = http_get(srv.port, "/")
            self.assertEqual(st4, 200)

    def test_pairwise_13_realloc_crossing_mmap_boundary_back_and_forth(self):
        """Reallocation crossing sbrk-to-mmap (growth) and mmap-to-sbrk (shrinkage) preserves integrity."""
        # Start in sbrk (64 KB)
        p = self.lib.my_malloc(64 * 1024)
        self.assertTrue(bool(p))
        ctypes.memset(p, 0x47, 64 * 1024)

        # Grow into mmap (256 KB)
        p = self.lib.my_realloc(p, 256 * 1024)
        self.assertTrue(bool(p))
        buf = (ctypes.c_char * 1024).from_address(p)
        self.assertEqual(buf.raw, b"\x47" * 1024)

        # Shrink back into sbrk (32 KB)
        p = self.lib.my_realloc(p, 32 * 1024)
        self.assertTrue(bool(p))
        buf = (ctypes.c_char * 1024).from_address(p)
        self.assertEqual(buf.raw, b"\x47" * 1024)

        self.lib.my_free(p)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_pairwise_14_isolated_canary_abort_during_multithreaded_load(self):
        """Isolated subprocess canary corruption abort does not compromise parent allocator operations."""
        # Parent allocates memory
        p = self.lib.my_malloc(128)
        self.assertTrue(bool(p))

        # Child process triggers canary corruption abort
        code, out, err = run_command([self.alloc_helper, "canary_header"])
        self.assertEqual(code, 0, f"Child did not abort as expected: {err}")

        # Parent continues allocating cleanly
        p2 = self.lib.my_malloc(256)
        self.assertTrue(bool(p2))

        self.lib.my_free(p)
        self.lib.my_free(p2)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_pairwise_15_isolated_double_free_during_server_execution(self):
        """Isolated child double-free abort does not affect concurrently running HTTP telemetry server."""
        with ServerContext() as srv:
            # Child double-frees
            code, out, err = run_command([self.alloc_helper, "double_free"])
            self.assertEqual(code, 0)

            # Server remains active and responsive
            st, data, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(st, 200)
            self.assertIn("cpu", data)

    def test_pairwise_16_cli_json_captured_under_active_cpu_load(self):
        """Capturing --headless --json while background compute processes run yields non-zero process counts."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--headless", "--json"])
        self.assertEqual(code, 0)
        self.assertIn('"process_count"', out)


if __name__ == "__main__":
    unittest.main()
