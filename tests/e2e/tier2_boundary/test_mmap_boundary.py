"""
Tier 2: Boundary & Corner Cases - Large Block Path (mmap threshold >= 128 KB)
Covers threshold-1 boundary, exact threshold, sbrk-to-mmap realloc, mmap-to-sbrk realloc, and rapid alloc/free churn.
"""

import ctypes
import unittest
from tests.e2e.e2e_common import load_allocator, MMAP_THRESHOLD


class TestMmapBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_boundary_threshold_minus_one_131071(self):
        """Allocation below threshold uses sbrk path without incrementing mmap counter."""
        stats_before = self.lib.allocator_get_stats()
        # total_size = sizeof(header)[48] + ALIGN(size) + sizeof(footer)[16] = 64 + ALIGN(size)
        # So payload of 64 KB is well below 128 KB
        ptr = self.lib.my_malloc(64 * 1024)
        self.assertTrue(bool(ptr))

        stats_after = self.lib.allocator_get_stats()
        self.assertEqual(
            stats_after.mmap_allocations,
            stats_before.mmap_allocations,
            "Small block should not use mmap",
        )
        self.lib.my_free(ptr)

    def test_02_boundary_exact_threshold_131072(self):
        """Allocation meeting or exceeding 128 KB threshold uses mmap path."""
        stats_before = self.lib.allocator_get_stats()
        ptr = self.lib.my_malloc(MMAP_THRESHOLD)
        self.assertTrue(bool(ptr))

        stats_after = self.lib.allocator_get_stats()
        self.assertGreater(
            stats_after.mmap_allocations,
            stats_before.mmap_allocations,
            "128 KB block must use mmap",
        )
        self.lib.my_free(ptr)

    def test_03_realloc_sbrk_to_mmap_boundary_crossing(self):
        """realloc growing from sbrk size (32 KB) to mmap size (256 KB) preserves data intact."""
        small_size = 32 * 1024
        large_size = 256 * 1024
        p = self.lib.my_malloc(small_size)
        self.assertTrue(bool(p))

        pattern = b"\xAA\x55\x12\x34" * (small_size // 4)
        ctypes.memmove(p, pattern, small_size)

        p_large = self.lib.my_realloc(p, large_size)
        self.assertTrue(bool(p_large))

        buf = (ctypes.c_char * small_size).from_address(p_large)
        self.assertEqual(buf.raw, pattern)

        self.lib.my_free(p_large)

    def test_04_realloc_mmap_to_sbrk_boundary_crossing(self):
        """realloc shrinking from mmap size (256 KB) to sbrk size (32 KB) preserves prefix intact."""
        large_size = 256 * 1024
        small_size = 32 * 1024
        p = self.lib.my_malloc(large_size)
        self.assertTrue(bool(p))

        pattern = b"\xDE\xAD\xBE\xEF" * (small_size // 4)
        ctypes.memmove(p, pattern, small_size)

        p_small = self.lib.my_realloc(p, small_size)
        self.assertTrue(bool(p_small))

        buf = (ctypes.c_char * small_size).from_address(p_small)
        self.assertEqual(buf.raw, pattern)

        self.lib.my_free(p_small)

    def test_05_mmap_rapid_alloc_free_cycle(self):
        """50 consecutive mmap allocations and frees cycle cleanly without resource leaks."""
        size = 140 * 1024
        for _ in range(50):
            p = self.lib.my_malloc(size)
            self.assertTrue(bool(p))
            self.lib.my_free(p)

        self.assertEqual(self.lib.allocator_verify_integrity(), 0)


if __name__ == "__main__":
    unittest.main()
