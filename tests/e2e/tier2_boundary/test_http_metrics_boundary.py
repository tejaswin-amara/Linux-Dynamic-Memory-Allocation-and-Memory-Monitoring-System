"""
Tier 2: Boundary & Corner Cases - HTTP Telemetry Daemon (/api/metrics)
Covers 404 route handling, 405 / invalid methods, concurrent request handling, time updates, and graceful SIGINT shutdown.
"""

import concurrent.futures
import signal
import time
import unittest
from tests.e2e.e2e_common import ServerContext, http_get, http_post


class TestHttpMetricsBoundary(unittest.TestCase):
    def test_01_unknown_endpoint_returns_404(self):
        """Requesting an unregistered route (e.g. GET /nonexistent) returns HTTP 404."""
        with ServerContext() as srv:
            status, _, _ = http_get(srv.port, "/nonexistent_route_404")
            self.assertEqual(status, 404, f"Expected 404 for unknown route, got {status}")

    def test_02_metrics_post_method_not_allowed_405(self):
        """Sending POST to /api/metrics returns HTTP 405 (or 404)."""
        with ServerContext() as srv:
            status, _, _ = http_post(srv.port, "/api/metrics", json_data={"foo": "bar"})
            self.assertIn(
                status,
                [404, 405],
                f"POST /api/metrics should return 405 Method Not Allowed or 404, got {status}",
            )

    def test_03_high_concurrency_get_metrics(self):
        """20 concurrent clients querying GET /api/metrics all receive HTTP 200 responses."""
        with ServerContext() as srv:
            def fetch_metrics():
                st, data, _ = http_get(srv.port, "/api/metrics", timeout=5)
                return st == 200 and isinstance(data, dict)

            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
                futures = [executor.submit(fetch_metrics) for _ in range(20)]
                results = [f.result() for f in concurrent.futures.as_completed(futures)]

            self.assertTrue(all(results), f"Some concurrent queries failed: {results.count(False)} failures")

    def test_04_dynamic_telemetry_updates_over_time(self):
        """Consecutive queries over time succeed and return valid timestamps."""
        with ServerContext() as srv:
            status1, data1, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(status1, 200)
            time.sleep(1.1)
            status2, data2, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(status2, 200)
            self.assertGreaterEqual(data2.get("timestamp", 0), data1.get("timestamp", 0))

    def test_05_clean_shutdown_on_sigint_within_deadline(self):
        """Daemon cleanly shuts down and releases bound socket upon receiving SIGINT within 2 seconds."""
        srv = ServerContext()
        with srv:
            self.assertIsNone(srv.proc.poll())
            srv.proc.send_signal(signal.SIGINT)
            start_time = time.time()
            srv.proc.wait(timeout=2.0)
            elapsed = time.time() - start_time
            self.assertLess(elapsed, 2.0, "Daemon shutdown took longer than 2.0s")
            self.assertEqual(srv.proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
