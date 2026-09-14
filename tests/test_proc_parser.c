#include "proc_parser.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_proc_parser_cpu(void) {
  cpu_metrics_t cpu;
  int res = proc_parser_read_cpu(&cpu);
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_TRUE(cpu.core_count >= 1);
  TEST_ASSERT_TRUE(cpu.total_usage_pct >= 0.0f &&
                   cpu.total_usage_pct <= 100.0f);
}

void test_proc_parser_mem(void) {
  mem_metrics_t mem;
  int res = proc_parser_read_mem(&mem);
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_TRUE(mem.mem_total_kb > 0);
  TEST_ASSERT_TRUE(mem.mem_available_kb > 0);
  TEST_ASSERT_TRUE(mem.mem_usage_pct >= 0.0f && mem.mem_usage_pct <= 100.0f);
}

void test_proc_parser_snapshot(void) {
  system_snapshot_t snapshot;
  int res = proc_parser_take_snapshot(&snapshot);
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_TRUE(snapshot.count > 0);
  TEST_ASSERT_TRUE(snapshot.cpu.core_count >= 1);
}

int main(void) {
  UnityBegin("test_proc_parser.c");
  RUN_TEST(test_proc_parser_cpu);
  RUN_TEST(test_proc_parser_mem);
  RUN_TEST(test_proc_parser_snapshot);
  return UnityEnd();
}
