"""
Tier 2: Boundary & Corner Cases - CLI Telemetry Interface (--json, --headless)
Covers stdout data stream purity, invalid CLI flags, invalid port numbers, non-numeric ports, and missing arguments.
"""

import json
import unittest
from tests.e2e.e2e_common import run_command, MEM_MONITOR_PATH


class TestCliTelemetryBoundary(unittest.TestCase):
    def test_01_pure_json_output_no_log_pollution(self):
        """mem_monitor --headless --json stdout should be valid unpolluted JSON (pure stdout stream)."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--headless", "--json"])
        self.assertEqual(code, 0)
        # Requirement M4.3: stdout must contain pure uncorrupted JSON parseable by automated tooling
        try:
            parsed = json.loads(out.strip())
            self.assertIn("cpu", parsed)
            self.assertIn("mem", parsed)
        except json.JSONDecodeError as exc:
            self.fail(f"stdout is not pure JSON (polluted by log messages): {exc}\nSTDOUT:\n{out}")

    def test_02_invalid_flag_exits_2(self):
        """Passing an unrecognized option (e.g. --unknown-flag) terminates with exit code 2."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--unknown-flag"])
        self.assertEqual(code, 2, f"Expected exit code 2, got {code}")
        self.assertIn("Unknown option", err + out)

    def test_03_port_out_of_range_exits_2(self):
        """Passing a port number out of range (>65535) terminates with exit code 2."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--port", "999999"])
        self.assertEqual(code, 2, f"Expected exit code 2, got {code}")
        self.assertIn("Invalid port", err + out)

    def test_04_port_non_numeric_exits_2(self):
        """Passing non-numeric port string terminates with exit code 2."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--port", "invalid_port"])
        self.assertEqual(code, 2, f"Expected exit code 2, got {code}")
        self.assertIn("Invalid port", err + out)

    def test_05_missing_option_argument_exits_2(self):
        """Passing an option missing its required value terminates with exit code 2."""
        code, out, err = run_command([MEM_MONITOR_PATH, "--port"])
        self.assertEqual(code, 2, f"Expected exit code 2, got {code}")
        self.assertIn("Missing value", err + out)


if __name__ == "__main__":
    unittest.main()
