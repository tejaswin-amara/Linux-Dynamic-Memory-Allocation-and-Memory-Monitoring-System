#include "allocator.h"
#include "free_list_internal.h"
#include "unity.h"
#include <sys/wait.h>
#include <unistd.h>

void setUp(void) { allocator_init(); }

void tearDown(void) { allocator_destroy(); }

void test_my_malloc_small(void) {
  void *p = my_malloc(64);
  TEST_ASSERT_NOT_NULL(p);

  /* Verify 16-byte alignment */
  TEST_ASSERT_EQUAL_UINT(0, ((uintptr_t)p) % 16);

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

void test_my_realloc_edge_cases(void) {
  void *p1 = my_realloc(NULL, 128);
  TEST_ASSERT_NOT_NULL(p1);

  void *p2 = my_realloc(p1, 0);
  TEST_ASSERT_NULL(p2);

  /* Shrink path splitting */
  void *p3 = my_malloc(1024);
  TEST_ASSERT_NOT_NULL(p3);
  void *p4 = my_realloc(p3, 64);
  TEST_ASSERT_NOT_NULL(p4);
  my_free(p4);
}

void test_my_malloc_overflow_rejection(void) {
  void *p = my_malloc((size_t)-48);
  TEST_ASSERT_NULL(p);
}

void test_double_free_abort(void) {
  pid_t pid = fork();
  if (pid == 0) {
    /* Child process: trigger double-free */
    void *ptr = my_malloc(64);
    my_free(ptr);
    my_free(ptr);
    exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  TEST_ASSERT_TRUE(WIFSIGNALED(status));
  TEST_ASSERT_EQUAL_INT(SIGABRT, WTERMSIG(status));
}

void test_realloc_uaf_abort(void) {
  pid_t pid = fork();
  if (pid == 0) {
    /* Child process: trigger realloc UAF */
    void *ptr = my_malloc(64);
    my_free(ptr);
    my_realloc(ptr, 128);
    exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  TEST_ASSERT_TRUE(WIFSIGNALED(status));
  TEST_ASSERT_EQUAL_INT(SIGABRT, WTERMSIG(status));
}

void test_canary_corruption_abort(void) {
  pid_t pid = fork();
  if (pid == 0) {
    /* Child process: corrupt header canary */
    void *ptr = my_malloc(64);
    block_header_t *hdr =
        (block_header_t *)((char *)ptr - sizeof(block_header_t));
    hdr->magic_header = 0x12345678;
    my_free(ptr);
    exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  TEST_ASSERT_TRUE(WIFSIGNALED(status));
  TEST_ASSERT_EQUAL_INT(SIGABRT, WTERMSIG(status));
}

static void *thread_malloc_worker(void *arg) {
  UNUSED(arg);
  for (int i = 0; i < 500; ++i) {
    void *p = my_malloc(128);
    if (p) {
      memset(p, 0xCC, 128);
      my_free(p);
    }
  }
  return NULL;
}

void test_allocator_multithreaded_stress(void) {
  pthread_t threads[4];
  for (int i = 0; i < 4; ++i) {
    pthread_create(&threads[i], NULL, thread_malloc_worker, NULL);
  }
  for (int i = 0; i < 4; ++i) {
    pthread_join(threads[i], NULL);
  }
  TEST_ASSERT_EQUAL_INT(0, allocator_verify_integrity());
}

void test_allocator_destroy_reset(void) {
  void *p = my_malloc(100);
  my_free(p);
  allocator_destroy();
  allocator_stats_t stats = allocator_get_stats();
  TEST_ASSERT_EQUAL_UINT(0, stats.total_allocated);
  TEST_ASSERT_EQUAL_UINT(0, stats.total_freed);
  allocator_init();
}

void test_integrity_rejects_invalid_block_size(void) {
  void *ptr = my_malloc(64);
  TEST_ASSERT_NOT_NULL(ptr);

  block_header_t *hdr =
      (block_header_t *)((char *)ptr - sizeof(block_header_t));
  hdr->block_size = 1;
  TEST_ASSERT_EQUAL_INT(-1, allocator_verify_integrity());
  hdr->block_size = sizeof(block_header_t) + ALIGN(64) + sizeof(block_footer_t);
  my_free(ptr);
}

void test_free_list_link_corruption_abort(void) {
  pid_t pid = fork();
  if (pid == 0) {
    void *ptr = my_malloc(64);
    my_free(ptr);

    block_header_t *block =
        (block_header_t *)((char *)ptr - sizeof(block_header_t));
    block->next = (block_header_t *)(uintptr_t)0x1;
    free_list_remove(block);
    exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  TEST_ASSERT_TRUE(WIFSIGNALED(status));
  TEST_ASSERT_EQUAL_INT(SIGABRT, WTERMSIG(status));
}

void test_coalescing(void) {
  allocator_stats_t before = allocator_get_stats();

  void *p1 = my_malloc(64);
  void *p2 = my_malloc(64);
  void *p3 = my_malloc(64);

  TEST_ASSERT_NOT_NULL(p1);
  TEST_ASSERT_NOT_NULL(p2);
  TEST_ASSERT_NOT_NULL(p3);

  my_free(p1);
  my_free(p3);
  my_free(p2);

  void *p4 = my_malloc(200);
  TEST_ASSERT_NOT_NULL(p4);

  allocator_stats_t after = allocator_get_stats();
  TEST_ASSERT_EQUAL_UINT(before.sbrk_allocations + 3, after.sbrk_allocations);

  my_free(p4);
}

int main(void) {
  UnityBegin("test_allocator.c");
  RUN_TEST(test_my_malloc_small);
  RUN_TEST(test_my_malloc_large_mmap);
  RUN_TEST(test_my_calloc_zeroes_memory);
  RUN_TEST(test_my_realloc_growth);
  RUN_TEST(test_my_realloc_edge_cases);
  RUN_TEST(test_my_malloc_overflow_rejection);
  RUN_TEST(test_double_free_abort);
  RUN_TEST(test_realloc_uaf_abort);
  RUN_TEST(test_canary_corruption_abort);
  RUN_TEST(test_allocator_multithreaded_stress);
  RUN_TEST(test_allocator_destroy_reset);
  RUN_TEST(test_integrity_rejects_invalid_block_size);
  RUN_TEST(test_free_list_link_corruption_abort);
  RUN_TEST(test_coalescing);
  return UnityEnd();
}
