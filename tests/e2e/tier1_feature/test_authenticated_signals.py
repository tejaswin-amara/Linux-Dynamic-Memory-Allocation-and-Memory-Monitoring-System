"""
Tier 1: Feature Coverage - Authenticated Signal Dispatch (/api/process/signal)
Covers token verification, acceptance of valid signals, 401 on missing/invalid tokens, and PID <= 1 protection.
"""

import os
import unittest
from tests.e2e.e2e_common import ServerContext, http_post


class TestAuthenticatedSignalsFeature(unittest.TestCase):
    def test_01_authenticated_signal_0_accepted(self):
        """Valid token with signal 0 to current PID succeeds with HTTP 200 OK."""
        token = "test-auth-signal-token-99"
        with ServerContext(token=token) as srv:
            payload = {"pid": os.getpid(), "signal": "0"}
            headers = {"X-Auth-Token": token}
            status, _, _ = http_post(srv.port, "/api/process/signal", headers=headers, json_data=payload)
            self.assertEqual(status, 200, f"Expected 200, got {status}")

    def test_02_unauthenticated_signal_rejected_401(self):
        """Missing X-Auth-Token header is rejected with HTTP 401 Unauthorized."""
        with ServerContext(token="my-secret-key") as srv:
            payload = {"pid": os.getpid(), "signal": "0"}
            status, _, _ = http_post(srv.port, "/api/process/signal", json_data=payload)
            self.assertEqual(status, 401, f"Expected 401, got {status}")

    def test_03_invalid_token_signal_rejected_401(self):
        """Incorrect X-Auth-Token header is rejected with HTTP 401 Unauthorized."""
        with ServerContext(token="correct-token") as srv:
            payload = {"pid": os.getpid(), "signal": "0"}
            headers = {"X-Auth-Token": "wrong-token"}
            status, _, _ = http_post(srv.port, "/api/process/signal", headers=headers, json_data=payload)
            self.assertEqual(status, 401, f"Expected 401, got {status}")

    def test_04_protected_pid_1_rejected(self):
        """Refuses to dispatch signals to system init PID 1."""
        token = "admin-token-123"
        with ServerContext(token=token) as srv:
            payload = {"pid": 1, "signal": "0"}
            headers = {"X-Auth-Token": token}
            status, _, _ = http_post(srv.port, "/api/process/signal", headers=headers, json_data=payload)
            self.assertIn(
                status,
                [400, 403, 500],
                f"PID 1 signal dispatch must be rejected, got {status}",
            )

    def test_05_protected_pid_0_rejected(self):
        """Refuses to dispatch signals to process group / kernel PID 0."""
        token = "admin-token-123"
        with ServerContext(token=token) as srv:
            payload = {"pid": 0, "signal": "0"}
            headers = {"X-Auth-Token": token}
            status, _, _ = http_post(srv.port, "/api/process/signal", headers=headers, json_data=payload)
            self.assertIn(
                status,
                [400, 403, 500],
                f"PID 0 signal dispatch must be rejected, got {status}",
            )


if __name__ == "__main__":
    unittest.main()
