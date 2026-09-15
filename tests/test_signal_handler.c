#include "common.h"
#include "unity.h"

int signal_send_to_process(pid_t pid, int sig);
int signal_parse_name(const char *name);

void setUp(void) {}
void tearDown(void) {}

void test_signal_parse_name(void) {
  TEST_ASSERT_EQUAL_INT(SIGKILL, signal_parse_name("SIGKILL"));
  TEST_ASSERT_EQUAL_INT(SIGKILL, signal_parse_name("9"));
  TEST_ASSERT_EQUAL_INT(SIGTERM, signal_parse_name("SIGTERM"));
  TEST_ASSERT_EQUAL_INT(SIGTERM, signal_parse_name("15"));
  TEST_ASSERT_EQUAL_INT(SIGSTOP, signal_parse_name("SIGSTOP"));
  TEST_ASSERT_EQUAL_INT(SIGSTOP, signal_parse_name("19"));
  TEST_ASSERT_EQUAL_INT(SIGCONT, signal_parse_name("SIGCONT"));
  TEST_ASSERT_EQUAL_INT(SIGCONT, signal_parse_name("18"));
  TEST_ASSERT_EQUAL_INT(SIGINT, signal_parse_name("SIGINT"));
  TEST_ASSERT_EQUAL_INT(SIGINT, signal_parse_name("2"));
  TEST_ASSERT_EQUAL_INT(SIGTERM, signal_parse_name("UNKNOWN"));
  TEST_ASSERT_EQUAL_INT(SIGTERM, signal_parse_name(NULL));
}

void test_signal_send_to_process(void) {
  // Test invalid PID
  TEST_ASSERT_EQUAL_INT(-1, signal_send_to_process(0, SIGKILL));
  TEST_ASSERT_EQUAL_INT(-1, signal_send_to_process(1, SIGKILL));

  // We shouldn't send random signals to arbitrary processes without care.
  // However, sending signal 0 (check if exists) to self should work.
  // Let's send signal 0 to our own process ID.
  TEST_ASSERT_EQUAL_INT(0, signal_send_to_process(getpid(), 0));
}

int main(void) {
  UnityBegin("test_signal_handler.c");
  RUN_TEST(test_signal_parse_name);
  RUN_TEST(test_signal_send_to_process);
  return UnityEnd();
}
