#include "allocator.h"
#include "free_list_internal.h"
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Direct unbuffered write logging to prevent recursive malloc and stdio deadlocks */
static void write_unbuffered(const char *buf, size_t len) {
  while (len > 0) {
    ssize_t ret = write(STDERR_FILENO, buf, len);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (ret == 0) {
      break;
    }
    buf += ret;
    len -= (size_t)ret;
  }
}

static void log_error_write(const char *file, int line, const char *fmt, ...) {
  int saved_errno = errno;
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  char buf[512];
  int n = snprintf(buf, sizeof(buf), "[ERROR] [%s:%d] %s\n", file, line, msg);
  if (n > 0) {
    write_unbuffered(buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
  }
  errno = saved_errno;
}

static void log_errno_error_write(const char *file, int line, int err, const char *fmt, ...) {
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  char buf[512];
  int n = snprintf(buf, sizeof(buf), "[ERROR] [%s:%d] %s (errno: %s)\n",
                   file, line, msg, strerror(err));
  if (n > 0) {
    write_unbuffered(buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
  }
  errno = err;
}

#undef LOG_ERROR
#define LOG_ERROR(...) log_error_write(__FILE__, __LINE__, __VA_ARGS__)

#undef LOG_ERRNO_ERROR
#define LOG_ERRNO_ERROR(...) log_errno_error_write(__FILE__, __LINE__, errno, __VA_ARGS__)

static pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static allocator_stats_t global_stats = {0};
static void *heap_start = NULL;
static void *initial_break = NULL;
static void *last_allocator_break = NULL;

static void fatal_abort(const char *msg) {
  write_unbuffered(msg, strlen(msg));
  abort();
}

static bool ensure_break_aligned(void) {
  uintptr_t cur_brk = (uintptr_t)sbrk(0);
  if (cur_brk == (uintptr_t)-1) {
    return false;
  }
  if ((cur_brk % ALIGNMENT) != 0) {
    size_t pad = ALIGNMENT - (cur_brk % ALIGNMENT);
    if (sbrk((intptr_t)pad) == (void *)-1) {
      return false;
    }
  }
  return true;
}

ALLOC_API void allocator_init(void) {
  pthread_mutex_lock(&alloc_mutex);
  memset(&global_stats, 0, sizeof(global_stats));
  free_list_reset();
  if (!ensure_break_aligned()) {
    pthread_mutex_unlock(&alloc_mutex);
    return;
  }
  void *cur = sbrk(0);
  if (cur != (void *)-1) {
    if (!initial_break) {
      initial_break = cur;
    }
    heap_start = initial_break;
    last_allocator_break = cur;
  }
  pthread_mutex_unlock(&alloc_mutex);
}

ALLOC_API void allocator_destroy(void) {
  pthread_mutex_lock(&alloc_mutex);
  memset(&global_stats, 0, sizeof(global_stats));
  free_list_reset();
  if (initial_break) {
    void *cur_brk = sbrk(0);
    if (cur_brk == last_allocator_break) {
      if (brk(initial_break) == 0) {
        initial_break = NULL;
        heap_start = NULL;
        last_allocator_break = NULL;
      } else {
        heap_start = initial_break;
      }
    } else {
      /* External runtime (e.g. Python host) expanded sbrk(0); avoid deallocating host memory */
      initial_break = NULL;
      heap_start = NULL;
      last_allocator_break = NULL;
    }
  } else {
    heap_start = NULL;
    last_allocator_break = NULL;
  }
  pthread_mutex_unlock(&alloc_mutex);
}

ALLOC_API allocator_stats_t allocator_get_stats(void) {
  pthread_mutex_lock(&alloc_mutex);
  allocator_stats_t stats = global_stats;
  pthread_mutex_unlock(&alloc_mutex);
  return stats;
}

ALLOC_API int allocator_verify_integrity(void) {
  pthread_mutex_lock(&alloc_mutex);
  int res = free_list_verify_integrity();
  if (res != 0) {
    pthread_mutex_unlock(&alloc_mutex);
    return res;
  }

  void *heap_end = sbrk(0);
  if (heap_start && heap_start != (void *)-1 && heap_end &&
      heap_end != (void *)-1 && (char *)heap_end >= (char *)heap_start) {
    char *curr = (char *)heap_start;
    while (curr < (char *)heap_end) {
      if ((size_t)((char *)heap_end - curr) <
          sizeof(block_header_t) + sizeof(block_footer_t)) {
        pthread_mutex_unlock(&alloc_mutex);
        return -1;
      }
      if (((uintptr_t)curr % ALIGNMENT) != 0) {
        pthread_mutex_unlock(&alloc_mutex);
        return -1;
      }
      block_header_t *hdr = (block_header_t *)curr;
      if (hdr->magic_header != ALLOC_MAGIC_HEADER ||
          hdr->block_size < sizeof(block_header_t) + sizeof(block_footer_t) ||
          (hdr->block_size % ALIGNMENT) != 0 ||
          (size_t)((char *)heap_end - curr) < hdr->block_size) {
        pthread_mutex_unlock(&alloc_mutex);
        return -1;
      }
      block_footer_t *ftr =
          (block_footer_t *)(curr + hdr->block_size - sizeof(block_footer_t));
      if (ftr->magic_footer != ALLOC_MAGIC_FOOTER ||
          ftr->block_size != hdr->block_size) {
        pthread_mutex_unlock(&alloc_mutex);
        return -1;
      }
      curr += hdr->block_size;
    }
  }

  pthread_mutex_unlock(&alloc_mutex);
  return 0;
}

static inline bool is_adjacent_free_block(block_header_t *block, void *heap_end) {
  if (!block || heap_end == (void *)-1 || !heap_start)
    return false;
  if ((char *)block < (char *)heap_start || (char *)block >= (char *)heap_end)
    return false;
  if ((size_t)((char *)heap_end - (char *)block) <
      sizeof(block_header_t) + sizeof(block_footer_t))
    return false;
  if (((uintptr_t)block % ALIGNMENT) != 0)
    return false;
  if (block->magic_header != ALLOC_MAGIC_HEADER || block->is_mmap != 0 ||
      !block->is_free)
    return false;
  if (block->block_size < sizeof(block_header_t) + sizeof(block_footer_t))
    return false;
  if ((block->block_size % ALIGNMENT) != 0)
    return false;
  if ((size_t)((char *)heap_end - (char *)block) < block->block_size)
    return false;
  block_footer_t *ftr =
      (block_footer_t *)((char *)block + block->block_size -
                         sizeof(block_footer_t));
  if (ftr->magic_footer != ALLOC_MAGIC_FOOTER ||
      ftr->block_size != block->block_size)
    return false;
  return true;
}

ALLOC_API void *my_malloc(size_t size) {
  if (size == 0)
    return NULL;

  size_t max_allowed = SIZE_MAX - 1024;
  if (size > max_allowed) {
    errno = ENOMEM;
    LOG_ERROR("Requested allocation size %zu causes integer overflow", size);
    return NULL;
  }

  size_t aligned_payload = ALIGN(size);
  size_t total_size =
      sizeof(block_header_t) + aligned_payload + sizeof(block_footer_t);

  pthread_mutex_lock(&alloc_mutex);

  if (total_size >= MMAP_THRESHOLD) {
    block_header_t *header = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (header == MAP_FAILED) {
      pthread_mutex_unlock(&alloc_mutex);
      LOG_ERRNO_ERROR("mmap syscall failed for size %zu", total_size);
      errno = ENOMEM;
      return NULL;
    }

    header->magic_header = ALLOC_MAGIC_HEADER;
    header->is_free = 0;
    header->requested_size = size;
    header->block_size = total_size;
    header->is_mmap = 1;
    header->next = NULL;
    header->prev = NULL;

    block_footer_t *footer = (block_footer_t *)((char *)header + total_size -
                                                sizeof(block_footer_t));
    footer->magic_footer = ALLOC_MAGIC_FOOTER;
    footer->block_size = total_size;

    global_stats.total_allocated += size;
    global_stats.mmap_allocations++;
    pthread_mutex_unlock(&alloc_mutex);

    return (void *)((char *)header + sizeof(block_header_t));
  }

  block_header_t *block = free_list_find_fit(total_size);

  if (!block) {
    if (!initial_break) {
      if (!ensure_break_aligned()) {
        pthread_mutex_unlock(&alloc_mutex);
        errno = ENOMEM;
        return NULL;
      }
      void *cur = sbrk(0);
      if (cur != (void *)-1) {
        initial_break = cur;
      }
    }
    if (!heap_start) {
      heap_start = initial_break;
    }
    if (!ensure_break_aligned()) {
      pthread_mutex_unlock(&alloc_mutex);
      errno = ENOMEM;
      return NULL;
    }
    void *heap_break = sbrk((intptr_t)total_size);
    if (heap_break == (void *)-1) {
      pthread_mutex_unlock(&alloc_mutex);
      LOG_ERRNO_ERROR("sbrk syscall failed for size %zu", total_size);
      errno = ENOMEM;
      return NULL;
    }
    block = (block_header_t *)heap_break;
    block->magic_header = ALLOC_MAGIC_HEADER;
    block->block_size = total_size;
    block->is_mmap = 0;
    block->next = NULL;
    block->prev = NULL;
    last_allocator_break = sbrk(0);

    global_stats.current_heap_size += total_size;
    global_stats.sbrk_allocations++;
  } else {
    size_t min_split = sizeof(block_header_t) + sizeof(block_footer_t) + 16;
    if (block->block_size >= total_size + min_split) {
      size_t remainder_size = block->block_size - total_size;
      block->block_size = total_size;

      block_header_t *remainder =
          (block_header_t *)((char *)block + total_size);
      remainder->magic_header = ALLOC_MAGIC_HEADER;
      remainder->block_size = remainder_size;
      remainder->is_free = 1;
      remainder->is_mmap = 0;
      remainder->next = NULL;
      remainder->prev = NULL;

      block_footer_t *rem_footer =
          (block_footer_t *)((char *)remainder + remainder_size -
                             sizeof(block_footer_t));
      rem_footer->magic_footer = ALLOC_MAGIC_FOOTER;
      rem_footer->block_size = remainder_size;

      free_list_insert(remainder);
    }
  }

  block->is_free = 0;
  block->requested_size = size;

  block_footer_t *footer =
      (block_footer_t *)((char *)block + block->block_size -
                         sizeof(block_footer_t));
  footer->magic_footer = ALLOC_MAGIC_FOOTER;
  footer->block_size = block->block_size;

  global_stats.total_allocated += size;
  pthread_mutex_unlock(&alloc_mutex);

  return (void *)((char *)block + sizeof(block_header_t));
}

ALLOC_API void my_free(void *ptr) {
  if (!ptr)
    return;

  pthread_mutex_lock(&alloc_mutex);

  block_header_t *header =
      (block_header_t *)((char *)ptr - sizeof(block_header_t));

  if (header->magic_header != ALLOC_MAGIC_HEADER) {
    pthread_mutex_unlock(&alloc_mutex);
    fatal_abort(
        "[FATAL] Heap corruption: Invalid magic header canary in my_free\n");
  }

  if (header->block_size < sizeof(block_header_t) + sizeof(block_footer_t)) {
    pthread_mutex_unlock(&alloc_mutex);
    fatal_abort("[FATAL] Heap corruption: Invalid block size in my_free\n");
  }

  if (header->is_mmap == 0) {
    void *heap_end = sbrk(0);
    if (!heap_start || heap_end == (void *)-1 ||
        (void *)header < heap_start || (void *)header >= heap_end ||
        (size_t)((char *)heap_end - (char *)header) < header->block_size) {
      pthread_mutex_unlock(&alloc_mutex);
      fatal_abort(
          "[FATAL] Heap corruption: Block bounds exceed heap limit in my_free\n");
    }
  } else {
    if ((uintptr_t)header > UINTPTR_MAX - header->block_size ||
        header->block_size < MMAP_THRESHOLD) {
      pthread_mutex_unlock(&alloc_mutex);
      fatal_abort(
          "[FATAL] Heap corruption: Invalid mmap block size in my_free\n");
    }
  }

  block_footer_t *footer =
      (block_footer_t *)((char *)header + header->block_size -
                         sizeof(block_footer_t));
  if (footer->magic_footer != ALLOC_MAGIC_FOOTER ||
      footer->block_size != header->block_size) {
    pthread_mutex_unlock(&alloc_mutex);
    fatal_abort(
        "[FATAL] Heap corruption: Invalid magic footer canary in my_free\n");
  }

  if (header->is_free) {
    pthread_mutex_unlock(&alloc_mutex);
    fatal_abort("[FATAL] Double free or use after free detected in my_free\n");
  }

  if (header->is_mmap) {
    global_stats.total_freed += header->requested_size;
    size_t block_size = header->block_size;
    pthread_mutex_unlock(&alloc_mutex);

    if (munmap(header, block_size) != 0) {
      LOG_ERRNO_ERROR("munmap syscall failed at %p", (void *)header);
    }
    return;
  }

  header->is_free = 1;
  global_stats.total_freed += header->requested_size;

  void *heap_end = sbrk(0);
  block_header_t *next_header =
      (block_header_t *)((char *)header + header->block_size);
  if (is_adjacent_free_block(next_header, heap_end)) {
    free_list_remove(next_header);
    header->block_size += next_header->block_size;
    block_footer_t *new_footer =
        (block_footer_t *)((char *)header + header->block_size -
                           sizeof(block_footer_t));
    new_footer->magic_footer = ALLOC_MAGIC_FOOTER;
    new_footer->block_size = header->block_size;
  }

  if (heap_start &&
      (char *)header >= (char *)heap_start + sizeof(block_header_t) + sizeof(block_footer_t)) {
    block_footer_t *prev_footer =
        (block_footer_t *)((char *)header - sizeof(block_footer_t));
    if (prev_footer->magic_footer == ALLOC_MAGIC_FOOTER &&
        prev_footer->block_size >= sizeof(block_header_t) + sizeof(block_footer_t) &&
        prev_footer->block_size <= (size_t)((char *)header - (char *)heap_start) &&
        (prev_footer->block_size % ALIGNMENT) == 0) {
      block_header_t *prev_header =
          (block_header_t *)((char *)header - prev_footer->block_size);
      if ((void *)prev_header >= heap_start &&
          ((uintptr_t)prev_header % ALIGNMENT) == 0 &&
          prev_header->magic_header == ALLOC_MAGIC_HEADER &&
          prev_header->block_size == prev_footer->block_size &&
          prev_header->is_mmap == 0 && prev_header->is_free) {
        free_list_remove(prev_header);
        prev_header->block_size += header->block_size;
        block_footer_t *new_footer =
            (block_footer_t *)((char *)prev_header + prev_header->block_size -
                               sizeof(block_footer_t));
        new_footer->magic_footer = ALLOC_MAGIC_FOOTER;
        new_footer->block_size = prev_header->block_size;
        header = prev_header;
      }
    }
  }

  free_list_insert(header);

  pthread_mutex_unlock(&alloc_mutex);
}

ALLOC_API void *my_calloc(size_t nmemb, size_t size) {
  if (nmemb == 0 || size == 0)
    return NULL;

  if (nmemb != 0 && size > SIZE_MAX / nmemb) {
    errno = ENOMEM;
    LOG_ERROR("Integer overflow detected in calloc arguments (%zu, %zu)", nmemb,
              size);
    return NULL;
  }

  size_t total = nmemb * size;
  void *ptr = my_malloc(total);
  if (ptr) {
    memset(ptr, 0, total);
  }
  return ptr;
}

ALLOC_API void *my_realloc(void *ptr, size_t size) {
  if (!ptr)
    return my_malloc(size);
  if (size == 0) {
    my_free(ptr);
    return NULL;
  }

  size_t max_allowed = SIZE_MAX - 1024;
  if (size > max_allowed) {
    errno = ENOMEM;
    return NULL;
  }

  pthread_mutex_lock(&alloc_mutex);

  block_header_t *header =
      (block_header_t *)((char *)ptr - sizeof(block_header_t));
  if (header->magic_header != ALLOC_MAGIC_HEADER) {
    pthread_mutex_unlock(&alloc_mutex);
    fatal_abort(
        "[FATAL] Heap corruption: Invalid magic header canary in my_realloc\n");
  }

  if (header->block_size < sizeof(block_header_t) + sizeof(block_footer_t)) {
    pthread_mutex_unlock(&alloc_mutex);
    fatal_abort("[FATAL] Heap corruption: Invalid block size in my_realloc\n");
  }

  if (header->is_mmap == 0) {
    void *heap_end = sbrk(0);
    if (!heap_start || heap_end == (void *)-1 ||
        (void *)header < heap_start || (void *)header >= heap_end ||
        (size_t)((char *)heap_end - (char *)header) < header->block_size) {
      pthread_mutex_unlock(&alloc_mutex);
      fatal_abort(
          "[FATAL] Heap corruption: Block bounds exceed heap limit in my_realloc\n");
    }
  } else {
    if ((uintptr_t)header > UINTPTR_MAX - header->block_size ||
        header->block_size < MMAP_THRESHOLD) {
      pthread_mutex_unlock(&alloc_mutex);
      fatal_abort(
          "[FATAL] Heap corruption: Invalid mmap block size in my_realloc\n");
    }
  }

  block_footer_t *footer =
      (block_footer_t *)((char *)header + header->block_size -
                         sizeof(block_footer_t));
  if (footer->magic_footer != ALLOC_MAGIC_FOOTER ||
      footer->block_size != header->block_size) {
    pthread_mutex_unlock(&alloc_mutex);
    fatal_abort(
        "[FATAL] Heap corruption: Invalid magic footer canary in my_realloc\n");
  }

  if (header->is_free) {
    pthread_mutex_unlock(&alloc_mutex);
    fatal_abort(
        "[FATAL] Double free or use after free detected in my_realloc\n");
  }

  if (header->is_mmap) {
    size_t old_requested_size = header->requested_size;
#if defined(__linux__) && defined(_GNU_SOURCE)
    size_t aligned_payload = ALIGN(size);
    size_t total_size =
        sizeof(block_header_t) + aligned_payload + sizeof(block_footer_t);

    if (total_size >= MMAP_THRESHOLD) {
      block_header_t *new_header =
          mremap(header, header->block_size, total_size, MREMAP_MAYMOVE);
      if (new_header != MAP_FAILED) {
        new_header->requested_size = size;
        new_header->block_size = total_size;
        block_footer_t *new_footer =
            (block_footer_t *)((char *)new_header + total_size -
                               sizeof(block_footer_t));
        new_footer->magic_footer = ALLOC_MAGIC_FOOTER;
        new_footer->block_size = total_size;
        global_stats.total_allocated += size;
        global_stats.total_freed += old_requested_size;
        pthread_mutex_unlock(&alloc_mutex);
        return (void *)((char *)new_header + sizeof(block_header_t));
      }
    }
#endif
    pthread_mutex_unlock(&alloc_mutex);

    void *new_ptr = my_malloc(size);
    if (!new_ptr)
      return NULL;

    size_t copy_size =
        old_requested_size < size ? old_requested_size : size;
    memcpy(new_ptr, ptr, copy_size);
    my_free(ptr);
    return new_ptr;
  }

  size_t aligned_payload = ALIGN(size);
  size_t total_needed =
      sizeof(block_header_t) + aligned_payload + sizeof(block_footer_t);
  size_t old_requested_size = header->requested_size;
  size_t min_split = sizeof(block_header_t) + sizeof(block_footer_t) + 16;
  void *heap_end = sbrk(0);

  /* Path 1: In-place shrink or in-block growth */
  if (header->block_size >= total_needed) {
    if (header->block_size >= total_needed + min_split) {
      size_t remainder_size = header->block_size - total_needed;
      header->block_size = total_needed;

      block_footer_t *hdr_footer =
          (block_footer_t *)((char *)header + total_needed -
                             sizeof(block_footer_t));
      hdr_footer->magic_footer = ALLOC_MAGIC_FOOTER;
      hdr_footer->block_size = total_needed;

      block_header_t *remainder =
          (block_header_t *)((char *)header + total_needed);
      remainder->magic_header = ALLOC_MAGIC_HEADER;
      remainder->block_size = remainder_size;
      remainder->is_free = 1;
      remainder->is_mmap = 0;
      remainder->next = NULL;
      remainder->prev = NULL;

      /* Forward coalescing on split remainder */
      block_header_t *next_after_rem =
          (block_header_t *)((char *)remainder + remainder_size);
      if (is_adjacent_free_block(next_after_rem, heap_end)) {
        free_list_remove(next_after_rem);
        remainder_size += next_after_rem->block_size;
        remainder->block_size = remainder_size;
      }

      block_footer_t *rem_footer =
          (block_footer_t *)((char *)remainder + remainder_size -
                             sizeof(block_footer_t));
      rem_footer->magic_footer = ALLOC_MAGIC_FOOTER;
      rem_footer->block_size = remainder_size;

      free_list_insert(remainder);
    }

    /* In-place telemetry accounting */
    if (size < old_requested_size) {
      global_stats.total_freed += (old_requested_size - size);
    } else if (size > old_requested_size) {
      global_stats.total_allocated += (size - old_requested_size);
    }

    header->requested_size = size;
    pthread_mutex_unlock(&alloc_mutex);
    return ptr;
  }

  /* Path 2: In-place forward expansion into adjacent free block */
  if (total_needed < MMAP_THRESHOLD) {
    block_header_t *next_block =
        (block_header_t *)((char *)header + header->block_size);
    if (is_adjacent_free_block(next_block, heap_end) &&
        header->block_size + next_block->block_size >= total_needed) {
      free_list_remove(next_block);
      size_t combined_size = header->block_size + next_block->block_size;

      if (combined_size >= total_needed + min_split) {
        size_t remainder_size = combined_size - total_needed;
        header->block_size = total_needed;

        block_footer_t *hdr_footer =
            (block_footer_t *)((char *)header + total_needed -
                               sizeof(block_footer_t));
        hdr_footer->magic_footer = ALLOC_MAGIC_FOOTER;
        hdr_footer->block_size = total_needed;

        block_header_t *remainder =
            (block_header_t *)((char *)header + total_needed);
        remainder->magic_header = ALLOC_MAGIC_HEADER;
        remainder->block_size = remainder_size;
        remainder->is_free = 1;
        remainder->is_mmap = 0;
        remainder->next = NULL;
        remainder->prev = NULL;

        block_footer_t *rem_footer =
            (block_footer_t *)((char *)remainder + remainder_size -
                               sizeof(block_footer_t));
        rem_footer->magic_footer = ALLOC_MAGIC_FOOTER;
        rem_footer->block_size = remainder_size;

        free_list_insert(remainder);
      } else {
        header->block_size = combined_size;
        block_footer_t *hdr_footer =
            (block_footer_t *)((char *)header + combined_size -
                               sizeof(block_footer_t));
        hdr_footer->magic_footer = ALLOC_MAGIC_FOOTER;
        hdr_footer->block_size = combined_size;
      }

      /* In-place expansion telemetry */
      if (size > old_requested_size) {
        global_stats.total_allocated += (size - old_requested_size);
      } else if (size < old_requested_size) {
        global_stats.total_freed += (old_requested_size - size);
      }

      header->requested_size = size;
      pthread_mutex_unlock(&alloc_mutex);
      return ptr;
    }
  }

  /* Path 3: Out-of-place relocation via malloc/memcpy/free */
  size_t old_requested_size_copy = old_requested_size;
  pthread_mutex_unlock(&alloc_mutex);

  void *new_ptr = my_malloc(size);
  if (!new_ptr)
    return NULL;

  size_t copy_size = old_requested_size_copy < size ? old_requested_size_copy : size;
  memcpy(new_ptr, ptr, copy_size);
  my_free(ptr);

  return new_ptr;
}
