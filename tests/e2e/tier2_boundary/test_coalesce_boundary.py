"""
Tier 2: Boundary & Corner Cases - Coalescing & Fragmentation (forward/backward)
Covers coalesce at heap_start boundary, heap_end boundary, realloc split remainder coalescing,
alternating fragmentation recovery, and allocator_destroy / reinit cycle.
"""

import unittest
from tests.e2e.e2e_common import load_allocator


class TestCoalesceBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_coalesce_at_heap_start_boundary(self):
        """Freeing the first heap block does not attempt backward coalescing beyond heap_start."""
        # First block on freshly initialized heap has no preceding block
        p1 = self.lib.my_malloc(128)
        self.assertTrue(bool(p1))
        p2 = self.lib.my_malloc(128)
        self.assertTrue(bool(p2))

        # Freeing first block p1 must check boundary tag safely
        self.lib.my_free(p1)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

        self.lib.my_free(p2)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_02_coalesce_at_heap_end_boundary(self):
        """Freeing the last heap block does not attempt forward coalescing past heap_end."""
        p1 = self.lib.my_malloc(64)
        p2 = self.lib.my_malloc(64)
        p3 = self.lib.my_malloc(64)
        self.assertTrue(bool(p1) and bool(p2) and bool(p3))

        # Free trailing block p3 at heap boundary
        self.lib.my_free(p3)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

        self.lib.my_free(p2)
        self.lib.my_free(p1)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_03_realloc_shrink_split_coalesce_with_free_neighbour(self):
        """realloc shrink split remainder coalesces forward with an already-free adjacent block."""
        p1 = self.lib.my_malloc(512)
        p2 = self.lib.my_malloc(512)
        self.assertTrue(bool(p1) and bool(p2))

        # Free p2, leaving a free block immediately following p1
        self.lib.my_free(p2)

        # Shrink p1 down to 64 bytes; remainder should coalesce with p2's free block
        p1_shrunk = self.lib.my_realloc(p1, 64)
        self.assertTrue(bool(p1_shrunk))

        self.assertEqual(self.lib.allocator_verify_integrity(), 0)
        self.lib.my_free(p1_shrunk)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_04_alternating_alloc_free_fragmentation_recovery(self):
        """Alternating alloc/free pattern (checkerboard) followed by full sweep coalesces completely."""
        ptrs = [self.lib.my_malloc(64) for _ in range(20)]
        for p in ptrs:
            self.assertTrue(bool(p))

        # Free odd indices
        for i in range(1, 20, 2):
            self.lib.my_free(ptrs[i])

        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

        # Free even indices
        for i in range(0, 20, 2):
            self.lib.my_free(ptrs[i])

        self.assertEqual(self.lib.allocator_verify_integrity(), 0)

    def test_05_allocator_destroy_and_reinit_heap_reset(self):
        """allocator_destroy() resets statistics and frees segregated lists; allocator_init reboots heap."""
        p = self.lib.my_malloc(100)
        self.lib.my_free(p)

        self.lib.allocator_destroy()
        stats = self.lib.allocator_get_stats()
        self.assertEqual(stats.total_allocated, 0)
        self.assertEqual(stats.total_freed, 0)

        self.lib.allocator_init()
        p2 = self.lib.my_malloc(100)
        self.assertTrue(bool(p2))
        self.lib.my_free(p2)
        self.assertEqual(self.lib.allocator_verify_integrity(), 0)


if __name__ == "__main__":
    unittest.main()
