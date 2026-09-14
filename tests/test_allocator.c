#include "allocator.h"
#include "unity.h"
#include <string.h>

void setUp(void) { allocator_init(); }

void tearDown(void) { allocator_destroy(); }

void test_my_malloc_small(void) {
  void *p = my_malloc(64);
  TEST_ASSERT_NOT_NULL(p);

  /* Verify 16-byte alignment */
  TEST_ASSERT_EQUAL_INT(0, ((uintptr_t)p) % 16);

  /* Write data and verify no segfault */
  memset(p, 0xAA, 64);
  my_free(p);
}

void test_my_malloc_large_mmap(void) {
  /* Larger than MMAP_THRESHOLD (128 KB) */
  size_t large_size = 256 * 1024;
  void *p = my_malloc(large_size);
  TEST_ASSERT_NOT_NULL(p);

  memset(p, 0xBB, large_size);

  block_header_t *hdr = (block_header_t *)((char *)p - sizeof(block_header_t));
  TEST_ASSERT_EQUAL_INT(ALLOC_MAGIC_HEADER, hdr->magic_header);
  TEST_ASSERT_EQUAL_INT(1, hdr->is_mmap);

  my_free(p);
}

void test_my_calloc_zeroes_memory(void) {
  size_t count = 32;
  size_t size = sizeof(int);
  int *arr = (int *)my_calloc(count, size);
  TEST_ASSERT_NOT_NULL(arr);

  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL_INT(0, arr[i]);
  }
  my_free(arr);
}

void test_my_realloc_growth(void) {
  char *orig = (char *)my_malloc(32);
  TEST_ASSERT_NOT_NULL(orig);
  strcpy(orig, "SystemsProgramming25CS2104E");

  char *grown = (char *)my_realloc(orig, 512);
  TEST_ASSERT_NOT_NULL(grown);
  TEST_ASSERT_EQUAL_STRING("SystemsProgramming25CS2104E", grown);

  my_free(grown);
}

void test_segregated_size_class_index(void) {
  TEST_ASSERT_EQUAL_INT(0, get_size_class_index(16));
  TEST_ASSERT_EQUAL_INT(1, get_size_class_index(48));
  TEST_ASSERT_EQUAL_INT(2, get_size_class_index(100));
  TEST_ASSERT_EQUAL_INT(9, get_size_class_index(64000));
}

void test_coalescing(void) {
  void *p1 = my_malloc(64);
  void *p2 = my_malloc(64);
  void *p3 = my_malloc(64);

  TEST_ASSERT_NOT_NULL(p1);
  TEST_ASSERT_NOT_NULL(p2);
  TEST_ASSERT_NOT_NULL(p3);

  // Free the first and third block
  my_free(p1);
  my_free(p3);

  // Free the middle block, this should trigger coalescing of p1, p2, and p3
  my_free(p2);

  // Allocate a block large enough to require coalescing of p1, p2, and p3
  void *p4 = my_malloc(192);
  TEST_ASSERT_NOT_NULL(p4);
  my_free(p4);
}

int main(void) {
  UnityBegin("test_allocator.c");
  RUN_TEST(test_my_malloc_small);
  RUN_TEST(test_my_malloc_large_mmap);
  RUN_TEST(test_my_calloc_zeroes_memory);
  RUN_TEST(test_my_realloc_growth);
  RUN_TEST(test_segregated_size_class_index);
  RUN_TEST(test_coalescing);
  return UnityEnd();
}
