"""
Tier 1: Feature Coverage - Initialized Allocation (calloc)
Covers zero-initialization, 16-byte alignment, write/read verification, clean release, and non-overlapping blocks.
"""

import ctypes
import unittest
from tests.e2e.e2e_common import load_allocator


class TestCallocFeature(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_calloc_zero_initialization(self):
        """calloc zeroes all allocated memory bytes."""
        count = 64
        size = 4  # 256 bytes total
        ptr = self.lib.my_calloc(count, size)
        self.assertTrue(bool(ptr), "my_calloc returned NULL")

        buf = (ctypes.c_char * (count * size)).from_address(ptr)
        self.assertEqual(buf.raw, b"\x00" * (count * size))
        self.lib.my_free(ptr)

    def test_02_calloc_16_byte_alignment(self):
        """calloc payload strictly guarantees 16-byte alignment."""
        for count in [1, 7, 16, 33]:
            for size in [1, 4, 8, 16]:
                ptr = self.lib.my_calloc(count, size)
                self.assertTrue(bool(ptr))
                self.assertEqual(ptr % 16, 0, f"calloc({count}, {size}) not 16-byte aligned")
                self.lib.my_free(ptr)

    def test_03_calloc_write_read_verification(self):
        """Write integer array to calloc buffer and verify contents."""
        count = 32
        elem_size = 4
        ptr = self.lib.my_calloc(count, elem_size)
        self.assertTrue(bool(ptr))

        int_array = (ctypes.c_int * count).from_address(ptr)
        for i in range(count):
            int_array[i] = i * 42

        for i in range(count):
            self.assertEqual(int_array[i], i * 42)

        self.lib.my_free(ptr)

    def test_04_calloc_free_succeeds_cleanly(self):
        """free(calloc_ptr) properly returns memory to free list."""
        stats_init = self.lib.allocator_get_stats()
        ptr = self.lib.my_calloc(100, 8)
        self.assertTrue(bool(ptr))
        stats_alloc = self.lib.allocator_get_stats()
        self.assertGreater(stats_alloc.total_allocated, stats_init.total_allocated)

        self.lib.my_free(ptr)
        stats_freed = self.lib.allocator_get_stats()
        self.assertGreater(stats_freed.total_freed, stats_init.total_freed)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_05_multiple_calloc_distinct_non_overlapping(self):
        """Sequential calloc calls allocate non-overlapping address ranges."""
        blocks = []
        size = 128
        for i in range(8):
            p = self.lib.my_calloc(1, size)
            self.assertTrue(bool(p))
            blocks.append((p, size))

        # Check disjointness
        for i in range(len(blocks)):
            p1, s1 = blocks[i]
            for j in range(i + 1, len(blocks)):
                p2, s2 = blocks[j]
                self.assertTrue(p1 + s1 <= p2 or p2 + s2 <= p1, f"Blocks {i} and {j} overlap!")

        for p, _ in blocks:
            self.lib.my_free(p)


if __name__ == "__main__":
    unittest.main()
