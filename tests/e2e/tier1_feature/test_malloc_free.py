"""
Tier 1: Feature Coverage - Standard Allocation (malloc, free)
Covers basic allocation, data persistence, 16-byte alignment, clean release, and block reuse.
"""

import ctypes
import unittest
from tests.e2e.e2e_common import load_allocator


class TestMallocFreeFeature(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_small_allocation_returns_valid_pointer(self):
        """malloc(64) returns a non-null pointer."""
        ptr = self.lib.my_malloc(64)
        self.assertIsNotNone(ptr, "my_malloc(64) returned NULL")
        self.assertNotEqual(ptr, 0, "my_malloc(64) returned 0 address")
        self.lib.my_free(ptr)

    def test_02_write_read_payload_integrity(self):
        """Memory returned by malloc can be written and read back intact."""
        size = 128
        ptr = self.lib.my_malloc(size)
        self.assertTrue(bool(ptr))

        # Write pattern 0x5A across buffer
        pattern = b"\x5A" * size
        ctypes.memmove(ptr, pattern, size)

        # Read back and compare
        buffer = (ctypes.c_char * size).from_address(ptr)
        self.assertEqual(buffer.raw, pattern)
        self.lib.my_free(ptr)

    def test_03_payload_16_byte_alignment(self):
        """Pointers returned by my_malloc strictly guarantee 16-byte alignment."""
        for size in [8, 16, 24, 48, 64, 100, 256, 1024]:
            ptr = self.lib.my_malloc(size)
            self.assertTrue(bool(ptr), f"Allocation failed for size {size}")
            self.assertEqual(
                ptr % 16, 0, f"Pointer {hex(ptr)} for size {size} is not 16-byte aligned"
            )
            self.lib.my_free(ptr)

    def test_04_free_releases_memory_cleanly(self):
        """Allocating and freeing memory succeeds without heap corruption."""
        ptrs = [self.lib.my_malloc(64) for _ in range(10)]
        for p in ptrs:
            self.assertTrue(bool(p))
            self.lib.my_free(p)

        # Verify heap integrity after freeing all
        integrity = self.lib.allocator_verify_integrity()
        self.assertEqual(integrity, 0, "Heap integrity check failed after freeing blocks")

    def test_05_reallocation_of_freed_block_reuses_memory(self):
        """Freed block is returned to segregated free list and reused for same size."""
        stats_before = self.lib.allocator_get_stats()
        p1 = self.lib.my_malloc(64)
        self.assertTrue(bool(p1))
        self.lib.my_free(p1)

        stats_freed = self.lib.allocator_get_stats()
        self.assertGreater(stats_freed.total_freed, stats_before.total_freed)

        # Subsequent malloc should succeed and reuse space without error
        p2 = self.lib.my_malloc(64)
        self.assertTrue(bool(p2))
        self.lib.my_free(p2)


if __name__ == "__main__":
    unittest.main()
