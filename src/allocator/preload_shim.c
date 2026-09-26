#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "allocator.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *(*real_malloc)(size_t) = NULL;
static void (*real_free)(void *) = NULL;
static void *(*real_calloc)(size_t, size_t) = NULL;
static void *(*real_realloc)(void *, size_t) = NULL;

static _Thread_local bool is_initializing = false;
static _Thread_local char bootstrap_buf[8192];
static _Thread_local size_t buf_offset = 0;

static void init_real_functions(void) {
  if (!real_malloc && !is_initializing) {
    is_initializing = true;
    *(void **)(&real_malloc) = dlsym(RTLD_NEXT, "malloc");
    *(void **)(&real_free) = dlsym(RTLD_NEXT, "free");
    *(void **)(&real_calloc) = dlsym(RTLD_NEXT, "calloc");
    *(void **)(&real_realloc) = dlsym(RTLD_NEXT, "realloc");
    is_initializing = false;
  }
}

static bool is_bootstrap_ptr(const void *ptr) {
  return (ptr >= (void *)bootstrap_buf &&
          ptr < (void *)(bootstrap_buf + sizeof(bootstrap_buf)));
}

void *malloc(size_t size) {
  if (is_initializing) {
    if (buf_offset + size <= sizeof(bootstrap_buf)) {
      void *p = &bootstrap_buf[buf_offset];
      buf_offset += ALIGN(size);
      return p;
    }
    return NULL;
  }
  init_real_functions();
  return my_malloc(size);
}

void free(void *ptr) {
  if (!ptr)
    return;
  if (is_bootstrap_ptr(ptr)) {
    return;
  }
  init_real_functions();
  my_free(ptr);
}

void *calloc(size_t nmemb, size_t size) {
  if (is_initializing) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p)
      memset(p, 0, total);
    return p;
  }
  init_real_functions();
  return my_calloc(nmemb, size);
}

void *realloc(void *ptr, size_t size) {
  if (!ptr)
    return malloc(size);
  if (is_bootstrap_ptr(ptr)) {
    void *new_p = malloc(size);
    if (new_p && size > 0) {
      memcpy(new_p, ptr, size < buf_offset ? size : buf_offset);
    }
    return new_p;
  }
  if (is_initializing) {
    return NULL;
  }
  init_real_functions();
  return my_realloc(ptr, size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
  if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0 ||
      alignment > ALIGNMENT) {
    return EINVAL;
  }
  size_t alloc_size = size < alignment ? alignment : size;
  void *p = malloc(alloc_size);
  if (!p) {
    return ENOMEM;
  }
  *memptr = p;
  return 0;
}

void *aligned_alloc(size_t alignment, size_t size) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      (size % alignment) != 0 || alignment > ALIGNMENT) {
    return NULL;
  }
  return malloc(size);
}

void *reallocarray(void *ptr, size_t nmemb, size_t size) {
  if (nmemb != 0 && size > SIZE_MAX / nmemb) {
    errno = ENOMEM;
    return NULL;
  }
  return realloc(ptr, nmemb * size);
}
