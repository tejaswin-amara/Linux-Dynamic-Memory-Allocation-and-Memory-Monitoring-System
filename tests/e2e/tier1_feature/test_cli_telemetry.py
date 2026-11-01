"""
Tier 1: Feature Coverage - CLI Telemetry Interface (--json, --headless)
Covers CLI help flags, exit codes, headless JSON output, and metric structure.
"""

import json
import re
import unittest
from tests.e2e.e2e_common import run_command, MEM_MONITOR_PATH


class TestCliTelemetryFeature(unittest.TestCase):
    def test_01_help_flag_exits_zero(self):
        """./mem_monitor --help displays usage information and terminates with exit code 0."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--help"])
        self.assertEqual(code, 0, f"Expected 0 exit, got {code}")
        combined = out + err
        self.assertIn("Usage:", combined)
        self.assertIn("--headless", combined)

    def test_02_short_h_flag_exits_zero(self):
        """./mem_monitor -h displays usage information and terminates with exit code 0."""
        code, out, err = run_command([MEM_MONITOR_PATH, "-h"])
        self.assertEqual(code, 0, f"Expected 0 exit, got {code}")
        combined = out + err
        self.assertIn("Usage:", combined)

    def test_03_headless_json_outputs_cpu_and_mem(self):
        """./mem_monitor --headless --json outputs telemetry containing cpu and mem blocks."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--headless", "--json"])
        self.assertEqual(code, 0, f"Command failed with {code}: {err}")
        self.assertIn('"cpu"', out)
        self.assertIn('"mem"', out)

    def test_04_headless_json_valid_json_structure(self):
        """Output from --headless --json parses as valid JSON with expected subkeys."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--headless", "--json"])
        self.assertEqual(code, 0)

        # Extract the JSON object from the output stream
        json_match = re.search(r"\{.*\}", out, re.DOTALL)
        self.assertIsNotNone(json_match, f"No JSON object found in output:\n{out}")

        data = json.loads(json_match.group(0))
        self.assertIn("cpu", data)
        self.assertIn("mem", data)
        self.assertIn("process_count", data)
        self.assertIn("total_usage_pct", data["cpu"])
        self.assertIn("mem_total_kb", data["mem"])

    def test_05_headless_json_process_count_matches_running(self):
        """--headless --json reports a positive process_count."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--headless", "--json"])
        self.assertEqual(code, 0)
        json_match = re.search(r"\{.*\}", out, re.DOTALL)
        self.assertIsNotNone(json_match)
        data = json.loads(json_match.group(0))
        self.assertGreater(data["process_count"], 0)


if __name__ == "__main__":
    unittest.main()
