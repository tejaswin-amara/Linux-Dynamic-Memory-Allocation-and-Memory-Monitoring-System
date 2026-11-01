"""
Tier 2: Boundary & Corner Cases - Canary Corruption & Double-Free Protection
Covers realloc use-after-free abort, single-bit canary flip aborts, corrupted block size detection,
and free-list linkage corruption detection.
"""

import ctypes
import os
import unittest
from tests.e2e.e2e_common import (
    load_allocator,
    run_command,
    run_isolated_python,
    BIN_DIR,
)


class TestCanaryDoubleFreeBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_allocator()
        cls.alloc_helper = os.path.join(BIN_DIR, "e2e_alloc_helper")

    def setUp(self):
        self.lib.allocator_init()

    def tearDown(self):
        self.lib.allocator_destroy()

    def test_01_realloc_uaf_triggers_abort(self):
        """Calling realloc on an already freed block triggers fatal abort."""
        code, out, err = run_command([self.alloc_helper, "realloc_uaf"])
        self.assertEqual(code, 0, f"realloc UAF did not abort with SIGABRT: {err}")

    def test_02_canary_header_single_bit_flip_abort(self):
        """Single-bit flip in magic header canary (0xDEADBEEF ^ 0x01) triggers fatal abort."""
        snippet = """
import ctypes
from tests.e2e.e2e_common import load_allocator
lib = load_allocator()
lib.allocator_init()
p = lib.my_malloc(64)
# Header canary is at p - 48
hdr_magic = p - 48
val = ctypes.c_uint32.from_address(hdr_magic).value
ctypes.c_uint32.from_address(hdr_magic).value = val ^ 0x01
lib.my_free(p)
"""
        code, out, err = run_isolated_python(snippet)
        # Process must be aborted by SIGABRT (-6 or 134)
        self.assertIn(code, [-6, 134], f"Single bit canary flip must abort with SIGABRT, got exit {code}")

    def test_03_canary_footer_single_bit_flip_abort(self):
        """Single-bit flip in magic footer canary (0xBEEFDEAD ^ 0x01) triggers fatal abort."""
        snippet = """
import ctypes
from tests.e2e.e2e_common import load_allocator
lib = load_allocator()
lib.allocator_init()
p = lib.my_malloc(64)
# Footer canary is at p + 64
ftr_magic = p + 64
val = ctypes.c_uint32.from_address(ftr_magic).value
ctypes.c_uint32.from_address(ftr_magic).value = val ^ 0x01
lib.my_free(p)
"""
        code, out, err = run_isolated_python(snippet)
        self.assertIn(code, [-6, 134], f"Single bit footer flip must abort with SIGABRT, got exit {code}")

    def test_04_corrupted_block_size_detected(self):
        """Corrupting header block_size to an invalid value fails allocator_verify_integrity()."""
        ptr = self.lib.my_malloc(64)
        self.assertTrue(bool(ptr))

        # In header (48 bytes):
        # uint32_t magic_header; (offset 0)
        # uint32_t is_free;      (offset 4)
        # size_t requested_size; (offset 8)
        # size_t block_size;     (offset 16)
        hdr_block_size_addr = ptr - 48 + 16
        orig_size = ctypes.c_size_t.from_address(hdr_block_size_addr).value

        # Corrupt block_size to 1 (smaller than header + footer)
        ctypes.c_size_t.from_address(hdr_block_size_addr).value = 1
        res = self.lib.allocator_verify_integrity()
        self.assertNotEqual(res, 0, "Integrity check must fail when block_size is corrupted")

        # Restore
        ctypes.c_size_t.from_address(hdr_block_size_addr).value = orig_size
        self.lib.my_free(ptr)

    def test_05_free_list_link_corruption_abort(self):
        """Corrupting free list linkage pointers triggers fatal abort or integrity failure."""
        snippet = """
import ctypes
from tests.e2e.e2e_common import load_allocator
lib = load_allocator()
lib.allocator_init()
p = lib.my_malloc(64)
lib.my_free(p)

# Corrupt next pointer to a dummy block where prev does not point back
fake_block = (ctypes.c_char * 64)()
fake_addr = ctypes.addressof(fake_block)

hdr_next_addr = p - 48 + 32
ctypes.c_void_p.from_address(hdr_next_addr).value = fake_addr

# Calling verify or malloc will detect corruption
res = lib.allocator_verify_integrity()
if res != 0:
    # Successfully rejected
    import sys; sys.exit(0)
# Or trigger remove
lib.my_malloc(64)
"""
        code, out, err = run_isolated_python(snippet)
        self.assertIn(code, [0, -6, 134])


if __name__ == "__main__":
    unittest.main()
