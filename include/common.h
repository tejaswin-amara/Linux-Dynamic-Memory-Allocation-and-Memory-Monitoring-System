#ifndef COMMON_H
#define COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define UNUSED(x) (void)(x)

/* Align allocator blocks to 16-byte boundaries. */
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

#define LOG_INFO(...)                                                          \
  do {                                                                         \
    fprintf(stdout, "[INFO] [%s:%d] ", __FILE__, __LINE__);                    \
    fprintf(stdout, __VA_ARGS__);                                              \
    fprintf(stdout, "\n");                                                     \
  } while (0)

#define LOG_WARN(...)                                                          \
  do {                                                                         \
    fprintf(stderr, "[WARN] [%s:%d] ", __FILE__, __LINE__);                   \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
  } while (0)

#define LOG_ERROR(...)                                                         \
  do {                                                                         \
    fprintf(stderr, "[ERROR] [%s:%d] ", __FILE__, __LINE__);                  \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, " (errno: %s)\n", strerror(errno));                      \
  } while (0)

#endif /* COMMON_H */
