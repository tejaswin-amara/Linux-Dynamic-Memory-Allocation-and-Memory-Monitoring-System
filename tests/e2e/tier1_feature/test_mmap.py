"""
Tier 1: Feature Coverage - Large Block Path (mmap threshold >= 128 KB)
Covers mmap routing, large block data integrity, clean munmap release, coexisting blocks, and 2 MB allocation.
"""

import ctypes
import unittest
from tests.e2e.e2e_common import load_allocator, MMAP_THRESHOLD


class TestMmapFeature(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_mmap_threshold_routing(self):
        """Allocations >= MMAP_THRESHOLD (128 KB) increment mmap_allocations counter."""
        stats_before = self.lib.allocator_get_stats()
        large_size = MMAP_THRESHOLD + 1024
        ptr = self.lib.my_malloc(large_size)
        self.assertTrue(bool(ptr), f"Allocation failed for size {large_size}")

        stats_after = self.lib.allocator_get_stats()
        self.assertGreater(
            stats_after.mmap_allocations,
            stats_before.mmap_allocations,
            "Large allocation was not routed to mmap",
        )
        self.lib.my_free(ptr)

    def test_02_mmap_large_payload_data_integrity(self):
        """Entire 256 KB mmap block payload is writable and readable intact."""
        size = 256 * 1024
        ptr = self.lib.my_malloc(size)
        self.assertTrue(bool(ptr))

        # Fill with alternating pattern
        ctypes.memset(ptr, 0x3C, size)

        buf = (ctypes.c_char * size).from_address(ptr)
        self.assertEqual(buf.raw[:1024], b"\x3C" * 1024)
        self.assertEqual(buf.raw[-1024:], b"\x3C" * 1024)

        self.lib.my_free(ptr)

    def test_03_mmap_free_releases_cleanly(self):
        """freeing an mmap block successfully cleans up memory."""
        size = 150 * 1024
        ptr = self.lib.my_malloc(size)
        self.assertTrue(bool(ptr))
        self.lib.my_free(ptr)

        integrity = self.lib.allocator_verify_integrity()
        self.assertEqual(integrity, 0, "Integrity check failed after mmap free")

    def test_04_multiple_mmap_allocations_coexist(self):
        """Multiple concurrent mmap blocks coexist without colliding."""
        ptrs = []
        sizes = [130 * 1024, 200 * 1024, 512 * 1024, 140 * 1024]
        for idx, sz in enumerate(sizes):
            p = self.lib.my_malloc(sz)
            self.assertTrue(bool(p))
            # Tag each buffer uniquely
            ctypes.memset(p, 0x10 + idx, sz)
            ptrs.append((p, sz, 0x10 + idx))

        # Verify all buffers retained their distinct tags
        for p, sz, tag in ptrs:
            b = (ctypes.c_ubyte * sz).from_address(p)
            self.assertEqual(b[0], tag)
            self.assertEqual(b[sz - 1], tag)

        for p, _, _ in ptrs:
            self.lib.my_free(p)

    def test_05_mmap_very_large_allocation_2mb(self):
        """2 MB allocation succeeds via mmap path and verifies memory integrity."""
        size = 2 * 1024 * 1024
        ptr = self.lib.my_malloc(size)
        self.assertTrue(bool(ptr), "2 MB allocation returned NULL")

        ctypes.memset(ptr, 0x99, 4096)
        ctypes.memset(ptr + size - 4096, 0x99, 4096)

        first_page = (ctypes.c_char * 4096).from_address(ptr)
        last_page = (ctypes.c_char * 4096).from_address(ptr + size - 4096)
        self.assertEqual(first_page.raw, b"\x99" * 4096)
        self.assertEqual(last_page.raw, b"\x99" * 4096)

        self.lib.my_free(ptr)


if __name__ == "__main__":
    unittest.main()
