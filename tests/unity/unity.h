#ifndef UNITY_FRAMEWORK_H
#define UNITY_FRAMEWORK_H

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

extern jmp_buf UnityTestJump;
extern int UnityTestsRun;
extern int UnityTestsFailed;
extern int UnityTestsIgnored;

void UnityBegin(const char *filename);
int UnityEnd(void);
void UnityTestFail(const char *message, const unsigned int line);

#define TEST_PROTECT() (setjmp(UnityTestJump) == 0)

#define TEST_ABORT() longjmp(UnityTestJump, 1)

#define RUN_TEST(func)                                                         \
  do {                                                                         \
    UnityTestsRun++;                                                           \
    printf("RUNNING: %s\n", #func);                                            \
    setUp();                                                                   \
    if (TEST_PROTECT()) {                                                      \
      func();                                                                  \
    }                                                                          \
    tearDown();                                                                \
  } while (0)

#define TEST_ASSERT(condition)                                                 \
  do {                                                                         \
    if (!(condition)) {                                                        \
      UnityTestFail("Assertion failed: " #condition, __LINE__);                \
      TEST_ABORT();                                                            \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_TRUE(condition) TEST_ASSERT(condition)
#define TEST_ASSERT_FALSE(condition) TEST_ASSERT(!(condition))
#define TEST_ASSERT_NULL(pointer) TEST_ASSERT((pointer) == NULL)
#define TEST_ASSERT_NOT_NULL(pointer) TEST_ASSERT((pointer) != NULL)
#define TEST_ASSERT_EQUAL_INT(expected, actual)                                \
  TEST_ASSERT((expected) == (actual))
#define TEST_ASSERT_EQUAL_PTR(expected, actual)                                \
  TEST_ASSERT((void *)(expected) == (void *)(actual))
#define TEST_ASSERT_EQUAL_STRING(expected, actual)                             \
  TEST_ASSERT(strcmp((expected), (actual)) == 0)

void setUp(void);
void tearDown(void);

#endif /* UNITY_FRAMEWORK_H */
