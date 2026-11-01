"""
Tier 1: Feature Coverage - Coalescing & Fragmentation (forward/backward)
Covers backward coalescing, forward coalescing, bidirectional coalescing, reuse for larger allocations, and integrity.
"""

import unittest
from tests.e2e.e2e_common import load_allocator


class TestCoalesceFeature(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_backward_coalescing_two_blocks(self):
        """Freeing block B when preceding block A is free coalesces backward."""
        p1 = self.lib.my_malloc(64)
        p2 = self.lib.my_malloc(64)
        self.assertTrue(bool(p1))
        self.assertTrue(bool(p2))

        self.lib.my_free(p1)
        self.lib.my_free(p2)  # Should backward coalesce with p1

        self.assertEqual(
            self.lib.allocator_verify_integrity(), 0, "Integrity failure after backward coalescing"
        )

    def test_02_forward_coalescing_two_blocks(self):
        """Freeing block A when succeeding block B is free coalesces forward."""
        p1 = self.lib.my_malloc(64)
        p2 = self.lib.my_malloc(64)
        self.assertTrue(bool(p1))
        self.assertTrue(bool(p2))

        self.lib.my_free(p2)
        self.lib.my_free(p1)  # Should forward coalesce with p2

        self.assertEqual(
            self.lib.allocator_verify_integrity(), 0, "Integrity failure after forward coalescing"
        )

    def test_03_bidirectional_coalescing_three_blocks(self):
        """Freeing middle block B when both adjacent blocks A and C are free merges all 3."""
        p1 = self.lib.my_malloc(64)
        p2 = self.lib.my_malloc(64)
        p3 = self.lib.my_malloc(64)
        self.assertTrue(bool(p1) and bool(p2) and bool(p3))

        self.lib.my_free(p1)
        self.lib.my_free(p3)
        self.lib.my_free(p2)  # Should merge p1, p2, p3 into one single free block

        self.assertEqual(
            self.lib.allocator_verify_integrity(), 0, "Integrity failure after 3-way coalescing"
        )

    def test_04_coalesced_block_satisfies_larger_allocation(self):
        """Coalesced chunk can immediately satisfy an allocation larger than any individual fragment."""
        stats_init = self.lib.allocator_get_stats()

        p1 = self.lib.my_malloc(64)
        p2 = self.lib.my_malloc(64)
        p3 = self.lib.my_malloc(64)
        self.assertTrue(bool(p1) and bool(p2) and bool(p3))

        self.lib.my_free(p1)
        self.lib.my_free(p3)
        self.lib.my_free(p2)

        # Allocate 180 bytes (exceeds individual 64-byte pieces)
        large = self.lib.my_malloc(180)
        self.assertTrue(bool(large))

        stats_large = self.lib.allocator_get_stats()
        # Should not have needed extra sbrk invocations because coalesced block fit it
        self.assertEqual(
            stats_large.sbrk_allocations,
            stats_init.sbrk_allocations + 3,
            "Allocation of coalesced block required extra sbrk instead of reusing coalesced space",
        )
        self.lib.my_free(large)

    def test_05_heap_integrity_valid_after_coalescing(self):
        """Heap boundary checks and free list links remain valid through multiple coalesce operations."""
        ptrs = [self.lib.my_malloc(48) for _ in range(6)]
        for p in ptrs:
            self.assertTrue(bool(p))

        # Free in non-linear pattern: 1, 3, 5, then 2, 4, 0
        self.lib.my_free(ptrs[1])
        self.lib.my_free(ptrs[3])
        self.lib.my_free(ptrs[5])
        self.lib.my_free(ptrs[2])
        self.lib.my_free(ptrs[4])
        self.lib.my_free(ptrs[0])

        self.assertEqual(self.lib.allocator_verify_integrity(), 0)


if __name__ == "__main__":
    unittest.main()
