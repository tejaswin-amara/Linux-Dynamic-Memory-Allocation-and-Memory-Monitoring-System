"""
Tier 1: Feature Coverage - Aligned Allocations (posix_memalign, aligned_alloc)
Covers 16-byte alignment guarantees, write/read data integrity, and clean deallocation.
"""

import os
import unittest
from tests.e2e.e2e_common import compile_c_code, run_under_preload, BIN_DIR


class TestAlignedFeature(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.makedirs(BIN_DIR, exist_ok=True)
        src = """
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *test = argv[1];

    if (strcmp(test, "posix_16") == 0) {
        void *p = NULL;
        int res = posix_memalign(&p, 16, 128);
        if (res != 0 || !p || ((uintptr_t)p % 16) != 0) return 2;
        free(p);
        return 0;
    } else if (strcmp(test, "aligned_16") == 0) {
        void *p = aligned_alloc(16, 128);
        if (!p || ((uintptr_t)p % 16) != 0) return 3;
        free(p);
        return 0;
    } else if (strcmp(test, "write_read") == 0) {
        void *p = NULL;
        if (posix_memalign(&p, 16, 256) != 0 || !p) return 4;
        memset(p, 0x4B, 256);
        unsigned char *b = (unsigned char *)p;
        for (int i = 0; i < 256; i++) {
            if (b[i] != 0x4B) { free(p); return 5; }
        }
        free(p);
        return 0;
    } else if (strcmp(test, "free_posix") == 0) {
        void *p = NULL;
        if (posix_memalign(&p, 16, 64) != 0 || !p) return 6;
        free(p);
        return 0;
    } else if (strcmp(test, "free_aligned") == 0) {
        void *p = aligned_alloc(16, 64);
        if (!p) return 7;
        free(p);
        return 0;
    }
    return 99;
}
"""
        cls.bin_path = os.path.join(BIN_DIR, "e2e_test_aligned_tier1")
        compile_c_code(src, cls.bin_path)

    def test_01_posix_memalign_standard_16_byte(self):
        """posix_memalign successfully allocates 16-byte aligned pointer."""
        code, out, err = run_under_preload([self.bin_path, "posix_16"])
        self.assertEqual(code, 0, f"posix_memalign failed (exit={code}): {err}")

    def test_02_aligned_alloc_standard_16_byte(self):
        """aligned_alloc successfully allocates 16-byte aligned pointer."""
        code, out, err = run_under_preload([self.bin_path, "aligned_16"])
        self.assertEqual(code, 0, f"aligned_alloc failed (exit={code}): {err}")

    def test_03_aligned_memory_write_read_integrity(self):
        """Aligned memory writes and reads verify completely without corruption."""
        code, out, err = run_under_preload([self.bin_path, "write_read"])
        self.assertEqual(code, 0, f"Memory write/read verification failed (exit={code}): {err}")

    def test_04_free_posix_memalign_pointer(self):
        """freeing posix_memalign pointer succeeds without fault."""
        code, out, err = run_under_preload([self.bin_path, "free_posix"])
        self.assertEqual(code, 0, f"Free posix_memalign pointer failed (exit={code}): {err}")

    def test_05_free_aligned_alloc_pointer(self):
        """freeing aligned_alloc pointer succeeds without fault."""
        code, out, err = run_under_preload([self.bin_path, "free_aligned"])
        self.assertEqual(code, 0, f"Free aligned_alloc pointer failed (exit={code}): {err}")


if __name__ == "__main__":
    unittest.main()
