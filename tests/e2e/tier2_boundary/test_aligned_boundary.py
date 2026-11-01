"""
Tier 2: Boundary & Corner Cases - Aligned Allocations (posix_memalign, aligned_alloc)
Covers rejection of non-power-of-two alignments, alignment < sizeof(void*), alignment 0,
size not multiple of alignment, and extended power-of-two alignments.
"""

import os
import unittest
from tests.e2e.e2e_common import compile_c_code, run_under_preload, BIN_DIR


class TestAlignedBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.makedirs(BIN_DIR, exist_ok=True)
        src = """
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *test = argv[1];

    if (strcmp(test, "non_power_of_two") == 0) {
        void *p = NULL;
        int res = posix_memalign(&p, 24, 64);
        if (res != EINVAL) return 2;
        return 0;
    } else if (strcmp(test, "less_than_pointer") == 0) {
        void *p = NULL;
        int res = posix_memalign(&p, 3, 64);
        if (res != EINVAL) return 3;
        return 0;
    } else if (strcmp(test, "aligned_alloc_zero") == 0) {
        void *p = aligned_alloc(0, 64);
        if (p != NULL) { free(p); return 4; }
        return 0;
    } else if (strcmp(test, "aligned_alloc_not_multiple") == 0) {
        void *p = aligned_alloc(16, 25);
        if (p != NULL) { free(p); return 5; }
        return 0;
    } else if (strcmp(test, "extended_alignments") == 0) {
        size_t aligns[] = {32, 64, 128, 256, 4096};
        for (int i = 0; i < 5; i++) {
            void *p = NULL;
            int res = posix_memalign(&p, aligns[i], 4096);
            if (res != 0 || !p || ((uintptr_t)p % aligns[i]) != 0) {
                if (p) free(p);
                return 6 + i;
            }
            free(p);
        }
        return 0;
    }
    return 99;
}
"""
        cls.bin_path = os.path.join(BIN_DIR, "e2e_test_aligned_tier2")
        compile_c_code(src, cls.bin_path)

    def test_01_posix_memalign_rejects_non_power_of_two(self):
        """posix_memalign rejects alignment 24 (not a power of two) with EINVAL."""
        code, out, err = run_under_preload([self.bin_path, "non_power_of_two"])
        self.assertEqual(code, 0, f"Expected rejection with EINVAL (exit=0), got {code}: {err}")

    def test_02_posix_memalign_rejects_alignment_less_than_pointer(self):
        """posix_memalign rejects alignment 3 (< sizeof(void*)) with EINVAL."""
        code, out, err = run_under_preload([self.bin_path, "less_than_pointer"])
        self.assertEqual(code, 0, f"Expected rejection with EINVAL (exit=0), got {code}: {err}")

    def test_03_aligned_alloc_rejects_zero_alignment(self):
        """aligned_alloc rejects alignment 0 and returns NULL."""
        code, out, err = run_under_preload([self.bin_path, "aligned_alloc_zero"])
        self.assertEqual(code, 0, f"aligned_alloc(0, 64) must return NULL: {err}")

    def test_04_aligned_alloc_rejects_size_not_multiple_of_alignment(self):
        """aligned_alloc rejects size that is not a multiple of alignment and returns NULL."""
        code, out, err = run_under_preload([self.bin_path, "aligned_alloc_not_multiple"])
        self.assertEqual(code, 0, f"aligned_alloc with non-multiple size must return NULL: {err}")

    def test_05_posix_memalign_extended_power_of_two_alignments(self):
        """posix_memalign supports extended power-of-two alignments (>16 bytes: 32, 64, 128, 256, 4096)."""
        code, out, err = run_under_preload([self.bin_path, "extended_alignments"])
        self.assertEqual(code, 0, f"posix_memalign failed for extended alignments (exit={code}): {err}")


if __name__ == "__main__":
    unittest.main()
