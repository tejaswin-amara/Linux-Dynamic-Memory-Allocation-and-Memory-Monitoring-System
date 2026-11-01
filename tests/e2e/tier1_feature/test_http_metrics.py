"""
Tier 1: Feature Coverage - HTTP Telemetry Daemon (/api/metrics)
Covers HTTP 200 OK response, JSON telemetry structure, CPU/RAM metrics, process list, and static web dashboard serving.
"""

import unittest
from tests.e2e.e2e_common import ServerContext, http_get


class TestHttpMetricsFeature(unittest.TestCase):
    def test_01_get_api_metrics_status_200(self):
        """GET /api/metrics returns HTTP 200 OK."""
        with ServerContext() as srv:
            status, _, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(status, 200, f"Expected 200, got {status}")

    def test_02_metrics_json_has_expected_keys(self):
        """GET /api/metrics payload contains cpu, mem, processes, and timestamp."""
        with ServerContext() as srv:
            status, data, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(status, 200)
            self.assertIsInstance(data, dict)
            self.assertIn("cpu", data)
            self.assertIn("mem", data)
            self.assertIn("processes", data)
            self.assertIn("timestamp", data)

    def test_03_metrics_reports_system_cpu_and_memory(self):
        """Metrics payload contains valid numeric fields for CPU and memory utilization."""
        with ServerContext() as srv:
            status, data, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(status, 200)
            self.assertIn("total_usage_pct", data["cpu"])
            self.assertIn("mem_total_kb", data["mem"])
            self.assertIn("mem_available_kb", data["mem"])
            self.assertGreater(data["mem"]["mem_total_kb"], 0)

    def test_04_metrics_includes_process_table(self):
        """Processes key contains an array of process entries with pid and comm."""
        with ServerContext() as srv:
            status, data, _ = http_get(srv.port, "/api/metrics")
            self.assertEqual(status, 200)
            procs = data.get("processes", [])
            self.assertIsInstance(procs, list)
            self.assertGreater(len(procs), 0)
            first = procs[0]
            self.assertIn("pid", first)
            self.assertIn("comm", first)
            self.assertIn("state", first)

    def test_05_static_dashboard_html_served(self):
        """HTTP daemon serves static dashboard index.html at root path."""
        with ServerContext() as srv:
            status, body, _ = http_get(srv.port, "/")
            self.assertEqual(status, 200)
            self.assertIn("<html", str(body).lower())


if __name__ == "__main__":
    unittest.main()
