#ifndef UNITY_FRAMEWORK_H
#define UNITY_FRAMEWORK_H

#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern jmp_buf UnityTestJump;
extern int UnityTestsRun;
extern int UnityTestsFailed;
extern int UnityTestsIgnored;

void UnityBegin(const char *filename);
int UnityEnd(void);
void UnityTestFail(const char *message, const unsigned int line);
void UnityTestIgnore(const char *message, const unsigned int line);

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

#define TEST_IGNORE()                                                          \
  do {                                                                         \
    UnityTestIgnore("Test ignored", __LINE__);                                 \
    TEST_ABORT();                                                              \
  } while (0)

#define TEST_ASSERT_TRUE(condition) TEST_ASSERT(condition)
#define TEST_ASSERT_FALSE(condition) TEST_ASSERT(!(condition))
#define TEST_ASSERT_NULL(pointer) TEST_ASSERT((pointer) == NULL)
#define TEST_ASSERT_NOT_NULL(pointer) TEST_ASSERT((pointer) != NULL)
#define TEST_ASSERT_EQUAL_INT(expected, actual)                                \
  TEST_ASSERT((expected) == (actual))
#define TEST_ASSERT_EQUAL_UINT(expected, actual)                               \
  TEST_ASSERT((uint64_t)(expected) == (uint64_t)(actual))
#define TEST_ASSERT_EQUAL_SIZE(expected, actual)                               \
  TEST_ASSERT((size_t)(expected) == (size_t)(actual))
#define TEST_ASSERT_EQUAL_PTR(expected, actual)                                \
  TEST_ASSERT((void *)(expected) == (void *)(actual))
#define TEST_ASSERT_EQUAL_STRING(expected, actual)                             \
  TEST_ASSERT(strcmp((expected), (actual)) == 0)
#define TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual)                      \
  TEST_ASSERT(fabs((double)(expected) - (double)(actual)) <= (double)(delta))

void setUp(void);
void tearDown(void);

#endif /* UNITY_FRAMEWORK_H */
