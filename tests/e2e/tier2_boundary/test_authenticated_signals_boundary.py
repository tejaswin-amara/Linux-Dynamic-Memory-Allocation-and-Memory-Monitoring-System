"""
Tier 2: Boundary & Corner Cases - Authenticated Signal Dispatch (/api/process/signal)
Covers nonexistent PID handling, invalid signal names, malformed JSON, missing keys, and strict key delimiter parsing.
"""

import unittest
from tests.e2e.e2e_common import ServerContext, http_post


class TestAuthenticatedSignalsBoundary(unittest.TestCase):
    def test_01_signal_nonexistent_pid_handled(self):
        """Signaling a non-existent PID returns an error status without crashing the server."""
        token = "test-token-signal-boundary"
        with ServerContext(token=token) as srv:
            payload = {"pid": 4194300, "signal": "0"}
            headers = {"X-Auth-Token": token}
            status, _, _ = http_post(srv.port, "/api/process/signal", headers=headers, json_data=payload)
            self.assertIn(
                status,
                [400, 404, 500],
                f"Signaling nonexistent PID should return error status, got {status}",
            )

    def test_02_signal_invalid_signal_name_rejected_400(self):
        """Invalid signal string (e.g. 'SIG_DOES_NOT_EXIST') is rejected with HTTP 400 Bad Request."""
        token = "test-token-signal-boundary"
        with ServerContext(token=token) as srv:
            payload = {"pid": 100, "signal": "SIG_DOES_NOT_EXIST"}
            headers = {"X-Auth-Token": token}
            status, _, _ = http_post(srv.port, "/api/process/signal", headers=headers, json_data=payload)
            self.assertEqual(status, 400, f"Expected 400 Bad Request, got {status}")

    def test_03_malformed_json_body_rejected_400(self):
        """Malformed JSON payload is rejected with HTTP 400 Bad Request."""
        token = "test-token-signal-boundary"
        with ServerContext(token=token) as srv:
            headers = {"X-Auth-Token": token}
            status, _, _ = http_post(
                srv.port,
                "/api/process/signal",
                headers=headers,
                raw_data="{bad_json_not_closing",
            )
            self.assertEqual(status, 400, f"Expected 400 Bad Request for malformed JSON, got {status}")

    def test_04_missing_required_keys_rejected_400(self):
        """JSON body missing required 'pid' field is rejected with HTTP 400 Bad Request."""
        token = "test-token-signal-boundary"
        with ServerContext(token=token) as srv:
            headers = {"X-Auth-Token": token}
            # Missing "pid"
            status, _, _ = http_post(
                srv.port,
                "/api/process/signal",
                headers=headers,
                json_data={"signal": "0"},
            )
            self.assertEqual(status, 400, f"Expected 400 Bad Request for missing pid, got {status}")

    def test_05_key_substring_injection_strict_validation(self):
        """Keys like 'xpid' or 'pid_extra' must not be confused with 'pid'."""
        token = "test-token-signal-boundary"
        with ServerContext(token=token) as srv:
            headers = {"X-Auth-Token": token}
            # Payload has 'fake_pid' but not 'pid'
            payload = {"fake_pid": 1234, "signal": "0"}
            status, _, _ = http_post(
                srv.port,
                "/api/process/signal",
                headers=headers,
                json_data=payload,
            )
            self.assertEqual(status, 400, f"Expected 400 for key confusion attempt, got {status}")


if __name__ == "__main__":
    unittest.main()
