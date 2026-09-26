#include "signal_handler.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_signal_parse_name(void) {
  TEST_ASSERT_EQUAL_INT(SIGKILL, signal_parse_name("SIGKILL"));
  TEST_ASSERT_EQUAL_INT(SIGKILL, signal_parse_name("9"));
  TEST_ASSERT_EQUAL_INT(SIGTERM, signal_parse_name("SIGTERM"));
  TEST_ASSERT_EQUAL_INT(SIGTERM, signal_parse_name("15"));
  TEST_ASSERT_EQUAL_INT(SIGSTOP, signal_parse_name("SIGSTOP"));
  TEST_ASSERT_EQUAL_INT(SIGCONT, signal_parse_name("SIGCONT"));
  TEST_ASSERT_EQUAL_INT(SIGINT, signal_parse_name("SIGINT"));
  TEST_ASSERT_EQUAL_INT(0, signal_parse_name("0"));
  TEST_ASSERT_EQUAL_INT(-1, signal_parse_name("INVALID_SIGNAL"));
  TEST_ASSERT_EQUAL_INT(-1, signal_parse_name("9999"));
}

void test_signal_send_to_process(void) {
  /* Valid signal to current process should succeed */
  TEST_ASSERT_EQUAL_INT(0, signal_send_to_process(getpid(), 0));

  /* Refuse PID <= 1 */
  TEST_ASSERT_EQUAL_INT(-1, signal_send_to_process(0, 0));
  TEST_ASSERT_EQUAL_INT(-1, signal_send_to_process(1, 0));
}

int main(void) {
  UnityBegin("test_signal_handler.c");
  RUN_TEST(test_signal_parse_name);
  RUN_TEST(test_signal_send_to_process);
  return UnityEnd();
}
