"""
Tier 1: Feature Coverage - Interposition & Bootstrap Arena (LD_PRELOAD)
Covers symbol interposition for standard POSIX allocation symbols: malloc, free, calloc, realloc, and reallocarray.
"""

import os
import unittest
from tests.e2e.e2e_common import compile_c_code, run_under_preload, BIN_DIR


class TestPreloadFeature(unittest.TestCase):
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
    const char *op = argv[1];

    if (strcmp(op, "malloc") == 0) {
        char *p = malloc(128);
        if (!p) return 2;
        strcpy(p, "PRELOAD_MALLOC_TEST");
        if (strcmp(p, "PRELOAD_MALLOC_TEST") != 0) { free(p); return 3; }
        free(p);
        return 0;
    } else if (strcmp(op, "calloc") == 0) {
        int *arr = calloc(32, sizeof(int));
        if (!arr) return 4;
        for (int i = 0; i < 32; i++) {
            if (arr[i] != 0) { free(arr); return 5; }
        }
        free(arr);
        return 0;
    } else if (strcmp(op, "realloc") == 0) {
        char *p = malloc(32);
        if (!p) return 6;
        strcpy(p, "REALLOC_TEST");
        char *p2 = realloc(p, 128);
        if (!p2) { free(p); return 7; }
        if (strcmp(p2, "REALLOC_TEST") != 0) { free(p2); return 8; }
        free(p2);
        return 0;
    } else if (strcmp(op, "reallocarray") == 0) {
        int *items = reallocarray(NULL, 16, sizeof(int));
        if (!items) return 9;
        for (int i = 0; i < 16; i++) items[i] = i * 2;
        for (int i = 0; i < 16; i++) {
            if (items[i] != i * 2) { free(items); return 10; }
        }
        free(items);
        return 0;
    }
    return 99;
}
"""
        cls.bin_path = os.path.join(BIN_DIR, "e2e_test_preload_tier1")
        compile_c_code(src, cls.bin_path)

    def test_01_preload_echo_command(self):
        """Standard /bin/echo runs cleanly under LD_PRELOAD interposition."""
        code, out, err = run_under_preload(["/bin/echo", "PRELOAD_SMOKE_TEST"])
        self.assertEqual(code, 0, f"/bin/echo failed under preload (exit={code}): {err}")
        self.assertIn("PRELOAD_SMOKE_TEST", out)

    def test_02_preload_malloc_program(self):
        """User application using standard malloc() runs successfully under LD_PRELOAD."""
        code, out, err = run_under_preload([self.bin_path, "malloc"])
        self.assertEqual(code, 0, f"Preload malloc test failed (exit={code}): {err}")

    def test_03_preload_calloc_program(self):
        """User application using standard calloc() runs successfully under LD_PRELOAD."""
        code, out, err = run_under_preload([self.bin_path, "calloc"])
        self.assertEqual(code, 0, f"Preload calloc test failed (exit={code}): {err}")

    def test_04_preload_realloc_program(self):
        """User application using standard realloc() runs successfully under LD_PRELOAD."""
        code, out, err = run_under_preload([self.bin_path, "realloc"])
        self.assertEqual(code, 0, f"Preload realloc test failed (exit={code}): {err}")

    def test_05_preload_reallocarray_program(self):
        """User application using standard reallocarray() runs successfully under LD_PRELOAD."""
        code, out, err = run_under_preload([self.bin_path, "reallocarray"])
        self.assertEqual(code, 0, f"Preload reallocarray test failed (exit={code}): {err}")


if __name__ == "__main__":
    unittest.main()
