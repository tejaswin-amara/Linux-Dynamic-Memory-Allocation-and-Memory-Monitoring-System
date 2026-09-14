#include "unity.h"
#include <string.h>

jmp_buf UnityTestJump;
int UnityTestsRun = 0;
int UnityTestsFailed = 0;
int UnityTestsIgnored = 0;
static const char *current_test_file = "";

void UnityBegin(const char *filename) {
  current_test_file = filename;
  UnityTestsRun = 0;
  UnityTestsFailed = 0;
  UnityTestsIgnored = 0;
  printf("\n==================== TEST SUITE: %s ====================\n",
         filename);
}

int UnityEnd(void) {
  printf("\n------------------------------------------------------------\n");
  printf("Tests Run: %d | Passed: %d | Failed: %d\n", UnityTestsRun,
         UnityTestsRun - UnityTestsFailed, UnityTestsFailed);
  printf("============================================================\n");
  return UnityTestsFailed;
}

void UnityTestFail(const char *message, const unsigned int line) {
  UnityTestsFailed++;
  printf("  [FAIL] %s:%u: %s\n", current_test_file, line, message);
}
