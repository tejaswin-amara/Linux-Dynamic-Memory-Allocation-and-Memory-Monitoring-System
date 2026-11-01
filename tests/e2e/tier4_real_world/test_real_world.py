"""
Tier 4: Real-World Application Scenarios
Complex end-to-end workloads simulating production environments:
1. High-throughput multi-threaded web server simulation under LD_PRELOAD
2. System monitoring daemon surviving rapid process churn
3. 10,000 operation random memory stress test through segregated free lists
4. Automated remote task management via authenticated REST API
5. Clean LD_PRELOAD interposition across standard Linux system utilities
"""

import concurrent.futures
import json
import os
import signal
import subprocess
import time
import unittest
from tests.e2e.e2e_common import (
    load_allocator,
    compile_c_code,
    run_command,
    run_under_preload,
    ServerContext,
    http_get,
    http_post,
    BIN_DIR,
    PROJECT_ROOT,
)


class TestRealWorldScenarios(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def test_scenario_01_web_server_simulation(self):
        """Scenario 1: High-throughput multi-threaded web server simulation under LD_PRELOAD."""
        src = """
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_WORKERS 8
#define REQS_PER_WORKER 50

static void *worker_loop(void *arg) {
    int id = *(int *)arg;
    for (int r = 0; r < REQS_PER_WORKER; r++) {
        // Allocate request buffer
        size_t req_sz = 128 + (r % 64);
        char *req = malloc(req_sz);
        if (!req) return (void *)1;
        snprintf(req, req_sz, "GET /item/%d_%d HTTP/1.1", id, r);

        // Dynamically growing response buffer via realloc
        size_t resp_sz = 256;
        char *resp = malloc(resp_sz);
        if (!resp) { free(req); return (void *)2; }
        strcpy(resp, "HTTP/1.1 200 OK\\r\\nContent-Type: text/plain\\r\\n\\r\\n");

        for (int chunk = 0; chunk < 4; chunk++) {
            resp_sz += 512;
            char *grown = realloc(resp, resp_sz);
            if (!grown) { free(resp); free(req); return (void *)3; }
            resp = grown;
            strcat(resp, "DATA_PAYLOAD_CHUNK_0123456789_");
        }

        free(req);
        free(resp);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_WORKERS];
    int ids[NUM_WORKERS];

    for (int i = 0; i < NUM_WORKERS; i++) {
        ids[i] = i;
        if (pthread_create(&threads[i], NULL, worker_loop, &ids[i]) != 0) return 10;
    }

    int fail = 0;
    for (int i = 0; i < NUM_WORKERS; i++) {
        void *res = NULL;
        pthread_join(threads[i], &res);
        if (res != NULL) fail++;
    }
    return fail;
}
"""
        bin_path = os.path.join(BIN_DIR, "e2e_web_server_sim")
        compile_c_code(src, bin_path)
        code, out, err = run_under_preload([bin_path])
        self.assertEqual(code, 0, f"Web server simulation failed under LD_PRELOAD (exit={code}): {err}")

    def test_scenario_02_system_monitoring_rapid_churn(self):
        """Scenario 2: Real-time system monitoring daemon under rapid process churn."""
        with ServerContext() as srv:
            # Spawn and kill 30 short-lived processes in batches
            for batch in range(6):
                procs = [subprocess.Popen(["/bin/sleep", "0.08"]) for _ in range(5)]
                # Poll metrics during process execution
                st, data, _ = http_get(srv.port, "/api/metrics")
                self.assertEqual(st, 200)
                self.assertIn("processes", data)
                for p in procs:
                    p.wait()

            # Final check that server is completely healthy
            st_final, data_final, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(st_final, 200)
            self.assertIn("cpu", data_final)

    def test_scenario_03_memory_stress_10000_churn(self):
        """Scenario 3: 10,000 mixed allocs, reallocs, and frees across all segregated bins."""
        alloc_helper = os.path.join(BIN_DIR, "e2e_alloc_helper")
        code, out, err = run_command([alloc_helper, "churn", "10000"], timeout=30)
        self.assertEqual(code, 0, f"10,000 operation churn failed: {err}")

    def test_scenario_04_authenticated_remote_task_management(self):
        """Scenario 4: Authenticated remote task management via REST API."""
        token = "admin-mgmt-token-777"
        # Start a dummy target process
        target_proc = subprocess.Popen(["/bin/sleep", "30"])
        target_pid = target_proc.pid

        try:
            with ServerContext(token=token) as srv:
                # 1. Verify unauthenticated attempt to signal is rejected
                unauth_st, _, _ = http_post(
                    srv.port,
                    "/api/process/signal",
                    json_data={"pid": target_pid, "signal": "0"},
                )
                self.assertEqual(unauth_st, 401, f"Expected 401 for unauth request, got {unauth_st}")

                # 2. Verify target process is alive with authenticated signal 0
                auth_headers = {"X-Auth-Token": token}
                ping_st, _, _ = http_post(
                    srv.port,
                    "/api/process/signal",
                    headers=auth_headers,
                    json_data={"pid": target_pid, "signal": "0"},
                )
                self.assertEqual(ping_st, 200, f"Expected 200 for auth ping, got {ping_st}")

                # 3. Dispatch SIGTERM to terminate target process
                term_st, _, _ = http_post(
                    srv.port,
                    "/api/process/signal",
                    headers=auth_headers,
                    json_data={"pid": target_pid, "signal": "SIGTERM"},
                )
                self.assertEqual(term_st, 200, f"Expected 200 for SIGTERM, got {term_st}")

                # 4. Wait for process to exit
                target_proc.wait(timeout=3.0)
                self.assertIsNotNone(target_proc.returncode)

                # 5. Subsequent signal to terminated process should report failure
                dead_st, _, _ = http_post(
                    srv.port,
                    "/api/process/signal",
                    headers=auth_headers,
                    json_data={"pid": target_pid, "signal": "0"},
                )
                self.assertIn(dead_st, [400, 404, 500])
        finally:
            if target_proc.poll() is None:
                target_proc.kill()
                target_proc.wait()

    def test_scenario_05_clean_ld_preload_standard_utilities(self):
        """Scenario 5: Clean LD_PRELOAD execution across standard Linux utilities."""
        commands = [
            ["/bin/ls", "-la", PROJECT_ROOT],
            ["/bin/grep", "CC", os.path.join(PROJECT_ROOT, "Makefile")],
            ["/bin/cat", os.path.join(PROJECT_ROOT, "README.md")],
            ["/bin/sleep", "0.05"],
            ["/usr/bin/find", os.path.join(PROJECT_ROOT, "include"), "-name", "*.h"],
            ["/usr/bin/env"],
        ]

        for cmd in commands:
            code, out, err = run_under_preload(cmd)
            self.assertEqual(
                code,
                0,
                f"Command '{' '.join(cmd)}' failed under LD_PRELOAD (exit={code}):\n{err}",
            )
            self.assertTrue(len(out) > 0 or "sleep" in cmd[0])


if __name__ == "__main__":
    unittest.main()
