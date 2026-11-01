#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "allocator.h"
#include <errno.h>
#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BOOTSTRAP_BUF_SIZE (128 * 1024)
#define BOOTSTRAP_MAGIC 0xFEEDC0DECAFEFACEULL
#define ALIGNED_BLOCK_MAGIC 0xA116ED01

typedef struct {
  uint64_t magic;
  size_t size;
} bootstrap_header_t;

typedef struct {
  uint32_t magic;
  uint32_t padding;
  size_t offset;
  size_t requested_size;
} aligned_header_t;

static char bootstrap_buf[BOOTSTRAP_BUF_SIZE] __attribute__((aligned(16)));
static _Atomic size_t bootstrap_offset = 0;
static _Thread_local bool in_allocator = false;

static bool is_bootstrap_ptr(const void *ptr) {
  return (ptr >= (void *)bootstrap_buf &&
          ptr < (void *)(bootstrap_buf + sizeof(bootstrap_buf)));
}

static void *bootstrap_alloc(size_t size) {
  if (size > BOOTSTRAP_BUF_SIZE - sizeof(bootstrap_header_t)) {
    return NULL;
  }
  size_t aligned_size = ALIGN(size);
  size_t chunk_total = sizeof(bootstrap_header_t) + aligned_size;

  size_t prev_offset = atomic_fetch_add(&bootstrap_offset, chunk_total);
  if (prev_offset + chunk_total > sizeof(bootstrap_buf)) {
    return NULL;
  }

  bootstrap_header_t *hdr = (bootstrap_header_t *)&bootstrap_buf[prev_offset];
  hdr->magic = BOOTSTRAP_MAGIC;
  hdr->size = size;

  return (void *)(hdr + 1);
}

static bool is_aligned_ptr(const void *ptr, block_header_t **out_real_hdr,
                           size_t *out_size) {
  if (!ptr || (uintptr_t)ptr < sizeof(block_header_t) + sizeof(void *)) {
    return false;
  }
  if (((uintptr_t)ptr & (sizeof(void *) - 1)) != 0) {
    return false;
  }
  const aligned_header_t *ah =
      (const aligned_header_t *)((const char *)ptr - sizeof(block_header_t));
  if (ah->magic == ALIGNED_BLOCK_MAGIC &&
      ah->offset >= sizeof(block_header_t) + sizeof(void *)) {
    if ((uintptr_t)ptr < ah->offset) {
      return false;
    }
    const block_header_t *real_hdr =
        (const block_header_t *)((const char *)ptr - ah->offset);
    if (real_hdr->magic_header == ALLOC_MAGIC_HEADER) {
      if (out_real_hdr) {
        *out_real_hdr = (block_header_t *)real_hdr;
      }
      if (out_size) {
        *out_size = ah->requested_size;
      }
      return true;
    }
  }
  return false;
}

static void *aligned_alloc_internal(size_t alignment, size_t alloc_size,
                                    size_t req_size) {
  if (alloc_size > SIZE_MAX - alignment - sizeof(block_header_t)) {
    errno = ENOMEM;
    return NULL;
  }

  size_t total = alloc_size + alignment + sizeof(block_header_t);
  void *base = NULL;
  if (in_allocator) {
    base = bootstrap_alloc(total);
  } else {
    in_allocator = true;
    base = my_malloc(total);
    in_allocator = false;
  }

  if (!base) {
    return NULL;
  }

  char *min_aligned = (char *)base + sizeof(block_header_t);
  uintptr_t raw = (uintptr_t)min_aligned;
  uintptr_t aligned = (raw + alignment - 1) & ~(alignment - 1);
  void *aligned_ptr = (void *)aligned;

  aligned_header_t *ah =
      (aligned_header_t *)((char *)aligned_ptr - sizeof(block_header_t));
  ah->magic = ALIGNED_BLOCK_MAGIC;
  ah->padding = 0;
  ah->offset =
      (size_t)((char *)aligned_ptr - ((char *)base - sizeof(block_header_t)));
  ah->requested_size = req_size;

  return aligned_ptr;
}

ALLOC_API void *malloc(size_t size) {
  if (in_allocator) {
    return bootstrap_alloc(size);
  }
  in_allocator = true;
  void *p = my_malloc(size);
  in_allocator = false;
  return p;
}

ALLOC_API void free(void *ptr) {
  if (!ptr) {
    return;
  }
  if (is_bootstrap_ptr(ptr)) {
    return;
  }
  block_header_t *real_hdr = NULL;
  if (is_aligned_ptr(ptr, &real_hdr, NULL)) {
    aligned_header_t *ah =
        (aligned_header_t *)((char *)ptr - sizeof(block_header_t));
    ah->magic = 0;
    void *base = (void *)((char *)real_hdr + sizeof(block_header_t));
    if (is_bootstrap_ptr(base)) {
      return;
    }
    my_free(base);
    return;
  }
  my_free(ptr);
}

ALLOC_API void *calloc(size_t nmemb, size_t size) {
  if (in_allocator) {
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
      errno = ENOMEM;
      return NULL;
    }
    size_t total = nmemb * size;
    void *p = bootstrap_alloc(total);
    if (p) {
      memset(p, 0, total);
    }
    return p;
  }
  in_allocator = true;
  void *p = my_calloc(nmemb, size);
  in_allocator = false;
  return p;
}

ALLOC_API void *realloc(void *ptr, size_t size) {
  if (!ptr) {
    return malloc(size);
  }
  if (is_bootstrap_ptr(ptr)) {
    if (size == 0) {
      return NULL;
    }
    bootstrap_header_t *hdr = (bootstrap_header_t *)ptr - 1;
    size_t copy_size = 0;
    if (hdr->magic == BOOTSTRAP_MAGIC) {
      copy_size = (hdr->size < size) ? hdr->size : size;
    }
    void *new_p = malloc(size);
    if (new_p && copy_size > 0) {
      memcpy(new_p, ptr, copy_size);
    }
    return new_p;
  }
  block_header_t *real_hdr = NULL;
  size_t orig_req_size = 0;
  if (is_aligned_ptr(ptr, &real_hdr, &orig_req_size)) {
    if (size == 0) {
      free(ptr);
      return NULL;
    }
    void *new_p = malloc(size);
    if (new_p) {
      size_t copy_size = (orig_req_size < size) ? orig_req_size : size;
      if (copy_size > 0) {
        memcpy(new_p, ptr, copy_size);
      }
      free(ptr);
    }
    return new_p;
  }
  if (in_allocator) {
    return NULL;
  }
  in_allocator = true;
  void *new_p = my_realloc(ptr, size);
  in_allocator = false;
  return new_p;
}

ALLOC_API int posix_memalign(void **memptr, size_t alignment, size_t size) {
  if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
    return EINVAL;
  }

  size_t alloc_size = (size == 0) ? alignment : size;
  void *p = NULL;
  if (alignment <= ALIGNMENT) {
    p = malloc(alloc_size);
  } else {
    p = aligned_alloc_internal(alignment, alloc_size, size);
  }

  if (!p) {
    return ENOMEM;
  }

  *memptr = p;
  return 0;
}

ALLOC_API void *aligned_alloc(size_t alignment, size_t size) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      (size % alignment) != 0) {
    errno = EINVAL;
    return NULL;
  }

  if (size == 0) {
    return NULL;
  }

  if (alignment <= ALIGNMENT) {
    return malloc(size);
  }

  return aligned_alloc_internal(alignment, size, size);
}

ALLOC_API void *reallocarray(void *ptr, size_t nmemb, size_t size) {
  if (nmemb != 0 && size > SIZE_MAX / nmemb) {
    errno = ENOMEM;
    return NULL;
  }
  return realloc(ptr, nmemb * size);
}

ALLOC_API size_t malloc_usable_size(void *ptr) {
  if (!ptr) {
    return 0;
  }
  if (is_bootstrap_ptr(ptr)) {
    bootstrap_header_t *bhdr = (bootstrap_header_t *)ptr - 1;
    if (bhdr->magic == BOOTSTRAP_MAGIC) {
      return bhdr->size;
    }
    return 0;
  }
  block_header_t *real_hdr = NULL;
  size_t req_size = 0;
  if (is_aligned_ptr(ptr, &real_hdr, &req_size)) {
    return req_size;
  }
  if ((uintptr_t)ptr >= sizeof(block_header_t)) {
    block_header_t *hdr =
        (block_header_t *)((char *)ptr - sizeof(block_header_t));
    if (hdr->magic_header == ALLOC_MAGIC_HEADER) {
      return hdr->requested_size;
    }
  }
  return 0;
}

ALLOC_API void *memalign(size_t alignment, size_t size) {
  if (alignment < sizeof(void *)) {
    alignment = sizeof(void *);
  }
  void *ptr = NULL;
  int ret = posix_memalign(&ptr, alignment, size);
  if (ret != 0) {
    errno = ret;
    return NULL;
  }
  return ptr;
}

ALLOC_API void *valloc(size_t size) {
  long pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize <= 0) {
    pagesize = 4096;
  }
  void *ptr = NULL;
  int ret = posix_memalign(&ptr, (size_t)pagesize, size);
  if (ret != 0) {
    errno = ret;
    return NULL;
  }
  return ptr;
}

ALLOC_API void *pvalloc(size_t size) {
  long pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize <= 0) {
    pagesize = 4096;
  }
  size_t ps = (size_t)pagesize;
  size_t rounded_size;
  if (size == 0) {
    rounded_size = ps;
  } else {
    if (size > SIZE_MAX - ps) {
      errno = ENOMEM;
      return NULL;
    }
    rounded_size = ((size + ps - 1) / ps) * ps;
  }
  void *ptr = NULL;
  int ret = posix_memalign(&ptr, ps, rounded_size);
  if (ret != 0) {
    errno = ret;
    return NULL;
  }
  return ptr;
}

