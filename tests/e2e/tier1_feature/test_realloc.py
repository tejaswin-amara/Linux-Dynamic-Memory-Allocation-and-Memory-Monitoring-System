"""
Tier 1: Feature Coverage - Reallocation (realloc)
Covers memory growth, shrinking, null pointer malloc equivalence, 16-byte alignment, and chained integrity.
"""

import ctypes
import unittest
from tests.e2e.e2e_common import load_allocator


class TestReallocFeature(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_realloc_growth_preserves_data(self):
        """realloc to a larger size preserves existing prefix contents."""
        orig_size = 64
        new_size = 512
        p1 = self.lib.my_malloc(orig_size)
        self.assertTrue(bool(p1))

        payload = b"E2E_REALLOC_TEST_STRING_ABCD_1234567890"[:orig_size]
        ctypes.memmove(p1, payload, len(payload))

        p2 = self.lib.my_realloc(p1, new_size)
        self.assertTrue(bool(p2))

        buf = (ctypes.c_char * len(payload)).from_address(p2)
        self.assertEqual(buf.raw, payload)

        self.lib.my_free(p2)

    def test_02_realloc_shrink_preserves_prefix(self):
        """realloc to a smaller size in-place or relocated preserves trimmed prefix."""
        orig_size = 512
        new_size = 64
        p1 = self.lib.my_malloc(orig_size)
        self.assertTrue(bool(p1))

        full_pattern = b"\x77" * orig_size
        ctypes.memmove(p1, full_pattern, orig_size)

        p2 = self.lib.my_realloc(p1, new_size)
        self.assertTrue(bool(p2))

        buf = (ctypes.c_char * new_size).from_address(p2)
        self.assertEqual(buf.raw, b"\x77" * new_size)

        self.lib.my_free(p2)

    def test_03_realloc_null_ptr_behaves_like_malloc(self):
        """realloc(NULL, size) behaves identically to malloc(size)."""
        ptr = self.lib.my_realloc(None, 128)
        self.assertTrue(bool(ptr), "realloc(NULL, 128) returned NULL")
        self.assertEqual(ptr % 16, 0)

        # Write and read back
        ctypes.memset(ptr, 0xAB, 128)
        self.lib.my_free(ptr)

    def test_04_realloc_preserves_16_byte_alignment(self):
        """Reallocated pointers maintain strict 16-byte alignment."""
        for orig in [32, 100, 250]:
            for target in [48, 512, 1024]:
                p = self.lib.my_malloc(orig)
                self.assertTrue(bool(p))
                p_re = self.lib.my_realloc(p, target)
                self.assertTrue(bool(p_re))
                self.assertEqual(p_re % 16, 0, f"realloc({orig} -> {target}) not aligned to 16 bytes")
                self.lib.my_free(p_re)

    def test_05_realloc_chained_operations_data_integrity(self):
        """Chained growth, shrinkage, and growth preserve original header data."""
        data = b"INTEGRITY_CHECK_CHAINED_REALLOC_OPS"
        p = self.lib.my_malloc(64)
        self.assertTrue(bool(p))
        ctypes.memmove(p, data, len(data))

        # Grow
        p = self.lib.my_realloc(p, 256)
        self.assertTrue(bool(p))
        buf = (ctypes.c_char * len(data)).from_address(p)
        self.assertEqual(buf.raw, data)

        # Shrink (still large enough for data)
        p = self.lib.my_realloc(p, 48)
        self.assertTrue(bool(p))
        buf = (ctypes.c_char * len(data)).from_address(p)
        self.assertEqual(buf.raw, data)

        # Grow again
        p = self.lib.my_realloc(p, 1024)
        self.assertTrue(bool(p))
        buf = (ctypes.c_char * len(data)).from_address(p)
        self.assertEqual(buf.raw, data)

        self.lib.my_free(p)


if __name__ == "__main__":
    unittest.main()
