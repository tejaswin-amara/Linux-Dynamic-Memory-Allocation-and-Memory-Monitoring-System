"""
Tier 2: Boundary & Corner Cases - Initialized Allocation (calloc)
Covers zero nmemb, zero size, both zero, multiplication overflow detection, and 1x1 byte allocation.
"""

import ctypes
import unittest
from tests.e2e.e2e_common import load_allocator


class TestCallocBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_calloc_zero_nmemb(self):
        """calloc(0, size) returns NULL or valid pointer without crash."""
        p = self.lib.my_calloc(0, 64)
        if p:
            self.lib.my_free(p)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_02_calloc_zero_size(self):
        """calloc(nmemb, 0) returns NULL or valid pointer without crash."""
        p = self.lib.my_calloc(64, 0)
        if p:
            self.lib.my_free(p)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_03_calloc_both_zero(self):
        """calloc(0, 0) returns NULL or valid pointer without crash."""
        p = self.lib.my_calloc(0, 0)
        if p:
            self.lib.my_free(p)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_04_calloc_integer_overflow_rejection(self):
        """calloc rejects multiplication overflow (nmemb * size > SIZE_MAX) and returns NULL."""
        size_max = (1 << 64) - 1
        # Overflows 64-bit size_t
        p = self.lib.my_calloc(size_max // 2 + 10, 3)
        self.assertFalse(bool(p), "calloc must reject multiplication overflow")

        p2 = self.lib.my_calloc(size_max, 2)
        self.assertFalse(bool(p2), "calloc must reject multiplication overflow")

    def test_05_calloc_single_byte(self):
        """calloc(1, 1) returns 16-byte aligned pointer containing 0x00."""
        p = self.lib.my_calloc(1, 1)
        self.assertTrue(bool(p), "calloc(1, 1) returned NULL")
        self.assertEqual(p % 16, 0)

        b = (ctypes.c_char * 1).from_address(p)
        self.assertEqual(b[0], b"\x00")
        self.lib.my_free(p)


if __name__ == "__main__":
    unittest.main()
