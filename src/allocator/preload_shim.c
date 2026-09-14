#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "allocator.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real glibc function pointers for fallback if needed during bootstrap */
static void *(*real_malloc)(size_t) = NULL;
static void (*real_free)(void *) = NULL;
static void *(*real_calloc)(size_t, size_t) = NULL;
static void *(*real_realloc)(void *, size_t) = NULL;

static bool is_initializing = false;

static void init_real_functions(void) {
  if (!real_malloc) {
    is_initializing = true;
    *(void **)(&real_malloc) = dlsym(RTLD_NEXT, "malloc");
    *(void **)(&real_free) = dlsym(RTLD_NEXT, "free");
    *(void **)(&real_calloc) = dlsym(RTLD_NEXT, "calloc");
    *(void **)(&real_realloc) = dlsym(RTLD_NEXT, "realloc");
    is_initializing = false;
  }
}

void *malloc(size_t size) {
  if (is_initializing) {
    static char bootstrap_buf[4096];
    static size_t buf_offset = 0;
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
  init_real_functions();
  return my_realloc(ptr, size);
}
