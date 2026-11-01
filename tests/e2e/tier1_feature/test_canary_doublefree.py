"""
Tier 1: Feature Coverage - Canary Corruption & Double-Free Protection
Covers magic header canary presence, magic footer canary presence, and fatal abort on double-free / corruption.
"""

import ctypes
import os
import unittest
from tests.e2e.e2e_common import (
    load_allocator,
    run_command,
    ALLOC_MAGIC_HEADER,
    ALLOC_MAGIC_FOOTER,
    BIN_DIR,
)


class TestCanaryDoubleFreeFeature(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()
        cls.alloc_helper = os.path.join(BIN_DIR, "e2e_alloc_helper")

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_canary_magic_header_present(self):
        """Header canary 0xDEADBEEF is correctly placed immediately preceding payload header boundary."""
        ptr = self.lib.my_malloc(64)
        self.assertTrue(bool(ptr))

        # Header is 48 bytes before payload pointer; first field is uint32_t magic_header
        hdr_magic_addr = ptr - 48
        magic = ctypes.c_uint32.from_address(hdr_magic_addr).value
        self.assertEqual(
            magic,
            ALLOC_MAGIC_HEADER,
            f"Expected magic header {hex(ALLOC_MAGIC_HEADER)}, got {hex(magic)}",
        )
        self.lib.my_free(ptr)

    def test_02_canary_magic_footer_present(self):
        """Footer canary 0xBEEFDEAD is present at the trailing boundary of allocated block."""
        size = 64
        ptr = self.lib.my_malloc(size)
        self.assertTrue(bool(ptr))

        # In allocator.c: aligned_payload = ALIGN(size) = 64
        # total_size = sizeof(block_header_t)[48] + aligned_payload[64] + sizeof(block_footer_t)[16] = 128
        # ftr is at (ptr - 48 + 128 - 16) = ptr + 64
        ftr_magic_addr = ptr + 64
        magic = ctypes.c_uint32.from_address(ftr_magic_addr).value
        self.assertEqual(
            magic,
            ALLOC_MAGIC_FOOTER,
            f"Expected magic footer {hex(ALLOC_MAGIC_FOOTER)}, got {hex(magic)}",
        )
        self.lib.my_free(ptr)

    def test_03_double_free_triggers_fatal_abort(self):
        """Attempting to double-free a memory chunk triggers a fatal SIGABRT."""
        code, out, err = run_command([self.alloc_helper, "double_free"])
        self.assertEqual(code, 0, f"Double free did not trigger expected SIGABRT: {err}")

    def test_04_header_canary_corruption_triggers_fatal_abort(self):
        """Corrupting header canary triggers fatal abort during free()."""
        code, out, err = run_command([self.alloc_helper, "canary_header"])
        self.assertEqual(code, 0, f"Header canary corruption did not trigger SIGABRT: {err}")

    def test_05_footer_canary_corruption_triggers_fatal_abort(self):
        """Corrupting footer canary triggers fatal abort during free()."""
        code, out, err = run_command([self.alloc_helper, "canary_footer"])
        self.assertEqual(code, 0, f"Footer canary corruption did not trigger SIGABRT: {err}")


if __name__ == "__main__":
    unittest.main()
