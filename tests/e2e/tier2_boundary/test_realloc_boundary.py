"""
Tier 2: Boundary & Corner Cases - Reallocation (realloc)
Covers realloc(p, 0), realloc(NULL, 0), exact same size, shrink to 1 byte, and overflow rejection leaving ptr intact.
"""

import ctypes
import unittest
from tests.e2e.e2e_common import load_allocator


class TestReallocBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_realloc_zero_size_frees_and_returns_null(self):
        """realloc(ptr, 0) frees ptr and returns NULL per POSIX specification."""
        p = self.lib.my_malloc(64)
        self.assertTrue(bool(p))
        res = self.lib.my_realloc(p, 0)
        self.assertFalse(bool(res), "realloc(ptr, 0) must return NULL")
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_02_realloc_null_and_zero_size(self):
        """realloc(NULL, 0) behaves like malloc(0) and returns NULL without crash."""
        res = self.lib.my_realloc(None, 0)
        self.assertFalse(bool(res))
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_03_realloc_same_size_in_place(self):
        """realloc to identical requested size retains original data and valid pointer."""
        size = 128
        p = self.lib.my_malloc(size)
        self.assertTrue(bool(p))
        ctypes.memset(p, 0x33, size)

        p2 = self.lib.my_realloc(p, size)
        self.assertTrue(bool(p2))

        buf = (ctypes.c_char * size).from_address(p2)
        self.assertEqual(buf.raw, b"\x33" * size)

        self.lib.my_free(p2)

    def test_04_realloc_shrink_to_one_byte(self):
        """realloc shrinking down to 1 byte preserves first byte accurately."""
        p = self.lib.my_malloc(256)
        self.assertTrue(bool(p))
        b = (ctypes.c_char * 256).from_address(p)
        b[0] = b"\x89"

        p2 = self.lib.my_realloc(p, 1)
        self.assertTrue(bool(p2))

        b2 = (ctypes.c_char * 1).from_address(p2)
        self.assertEqual(b2[0], b"\x89")

        self.lib.my_free(p2)

    def test_05_realloc_integer_overflow_size_preserves_original(self):
        """realloc with massive overflow size returns NULL and preserves original buffer."""
        size = 64
        p = self.lib.my_malloc(size)
        self.assertTrue(bool(p))
        ctypes.memset(p, 0x44, size)

        size_max = (1 << 64) - 1
        failed_p = self.lib.my_realloc(p, size_max)
        self.assertFalse(bool(failed_p), "realloc with overflow size must return NULL")

        # Original pointer p must still be valid and unmodified
        buf = (ctypes.c_char * size).from_address(p)
        self.assertEqual(buf.raw, b"\x44" * size)

        self.lib.my_free(p)


if __name__ == "__main__":
    unittest.main()
