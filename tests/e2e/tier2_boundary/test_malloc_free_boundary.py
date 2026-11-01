"""
Tier 2: Boundary & Corner Cases - Standard Allocation (malloc, free)
Covers size 0, free(NULL), 1 byte, power-of-two boundaries, and SIZE_MAX integer overflow rejection.
"""

import ctypes
import unittest
from tests.e2e.e2e_common import load_allocator


class TestMallocFreeBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_malloc_zero_bytes(self):
        """malloc(0) safely returns NULL or a unique pointer that can be freed."""
        ptr = self.lib.my_malloc(0)
        # Per POSIX, malloc(0) either returns NULL or a non-null pointer that may be passed to free()
        if ptr:
            self.lib.my_free(ptr)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_02_free_null_pointer_is_noop(self):
        """free(NULL) is guaranteed to be a safe no-op without segfault or error."""
        self.lib.my_free(None)
        self.lib.my_free(0)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_03_malloc_one_byte(self):
        """malloc(1) returns 16-byte aligned pointer with valid writable single byte."""
        ptr = self.lib.my_malloc(1)
        self.assertTrue(bool(ptr), "malloc(1) returned NULL")
        self.assertEqual(ptr % 16, 0, "malloc(1) must be 16-byte aligned")

        # Write single byte
        b = (ctypes.c_char * 1).from_address(ptr)
        b[0] = b"\x7F"
        self.assertEqual(b[0], b"\x7F")

        self.lib.my_free(ptr)

    def test_04_malloc_power_of_two_boundaries(self):
        """Allocations adjacent to powers of two (2^k - 1, 2^k, 2^k + 1) preserve integrity."""
        test_sizes = [15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257]
        ptrs = []
        for sz in test_sizes:
            p = self.lib.my_malloc(sz)
            self.assertTrue(bool(p), f"malloc({sz}) failed")
            self.assertEqual(p % 16, 0)
            ctypes.memset(p, 0xEE, sz)
            ptrs.append(p)

        for p in ptrs:
            self.lib.my_free(p)

        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_05_malloc_size_max_overflow_rejection(self):
        """malloc with size near SIZE_MAX is rejected and returns NULL."""
        size_max = (1 << 64) - 1
        for sz in [size_max, size_max - 1, size_max - 1000]:
            ptr = self.lib.my_malloc(sz)
            self.assertFalse(bool(ptr), f"malloc({sz}) should have returned NULL on overflow")


if __name__ == "__main__":
    unittest.main()
