"""
Tier 1: Feature Coverage - Proc Telemetry Parser (CPU, Memory, State)
Covers CPU parsing, core count detection, memory metrics calculation, and process table parsing.
"""

import json
import os
import unittest
from tests.e2e.e2e_common import run_command, BIN_DIR


class TestProcTelemetryFeature(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.helper = os.path.join(BIN_DIR, "e2e_proc_helper")

    def test_01_cpu_metrics_in_range(self):
        """proc_parser_read_cpu computes CPU usage percentage within [0.0, 100.0]."""
        code, out, err = run_command([self.helper, "cpu"])
        self.assertEqual(code, 0, f"e2e_proc_helper cpu failed: {err}")
        data = json.loads(out.strip())
        self.assertIn("total_usage_pct", data)
        self.assertGreaterEqual(data["total_usage_pct"], 0.0)
        self.assertLessEqual(data["total_usage_pct"], 100.0)

    def test_02_core_count_positive(self):
        """proc_parser detects positive CPU core count."""
        code, out, err = run_command([self.helper, "cpu"])
        self.assertEqual(code, 0, f"e2e_proc_helper cpu failed: {err}")
        data = json.loads(out.strip())
        self.assertIn("core_count", data)
        self.assertGreaterEqual(data["core_count"], 1)

    def test_03_mem_total_and_available_positive(self):
        """proc_parser reports non-zero total and available RAM from /proc/meminfo."""
        code, out, err = run_command([self.helper, "mem"])
        self.assertEqual(code, 0, f"e2e_proc_helper mem failed: {err}")
        data = json.loads(out.strip())
        self.assertIn("mem_total_kb", data)
        self.assertIn("mem_available_kb", data)
        self.assertGreater(data["mem_total_kb"], 0)
        self.assertGreater(data["mem_available_kb"], 0)
        self.assertLessEqual(data["mem_available_kb"], data["mem_total_kb"])

    def test_04_process_list_has_valid_pids(self):
        """Snapshot gathers list of running processes with valid PIDs >= 1."""
        code, out, err = run_command([self.helper, "snapshot"])
        self.assertEqual(code, 0, f"e2e_proc_helper snapshot failed: {err}")
        data = json.loads(out.strip())
        self.assertIn("count", data)
        self.assertGreater(data["count"], 0)
        self.assertGreaterEqual(data["first_pid"], 1)

    def test_05_process_state_valid_ascii(self):
        """Process state is a valid Linux task state code ('R', 'S', 'D', 'Z', 'T', 'I', etc.)."""
        code, out, err = run_command([self.helper, "snapshot"])
        self.assertEqual(code, 0, f"e2e_proc_helper snapshot failed: {err}")
        data = json.loads(out.strip())
        valid_states = {"R", "S", "D", "Z", "T", "t", "W", "X", "I"}
        self.assertIn(data["first_state"], valid_states)


if __name__ == "__main__":
    unittest.main()
