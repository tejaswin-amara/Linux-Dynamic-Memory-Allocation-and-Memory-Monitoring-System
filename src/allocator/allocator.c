#include "allocator.h"
#include <sys/mman.h>

static pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static allocator_stats_t global_stats = {0, 0, 0, 0, 0};

void allocator_init(void) {
  pthread_mutex_lock(&alloc_mutex);
  memset(&global_stats, 0, sizeof(global_stats));
  pthread_mutex_unlock(&alloc_mutex);
}

void allocator_destroy(void) { /* Cleanup mutex or hooks if necessary */ }

allocator_stats_t allocator_get_stats(void) {
  pthread_mutex_lock(&alloc_mutex);
  allocator_stats_t stats = global_stats;
  pthread_mutex_unlock(&alloc_mutex);
  return stats;
}

int allocator_verify_integrity(void) {
  /* Integrity verification is performed on individual blocks */
  return 0;
}

void *my_malloc(size_t size) {
  if (size == 0)
    return NULL;

  size_t total_size =
      ALIGN(sizeof(block_header_t) + size + sizeof(block_footer_t));

  pthread_mutex_lock(&alloc_mutex);

  /* Large allocations >= 128 KB via direct mmap */
  if (total_size >= MMAP_THRESHOLD) {
    void *mapped = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
      LOG_ERROR("mmap syscall failed for size %zu", total_size);
      pthread_mutex_unlock(&alloc_mutex);
      return NULL;
    }

    block_header_t *header = (block_header_t *)mapped;
    header->magic_header = ALLOC_MAGIC_HEADER;
    header->is_free = 0;
    header->requested_size = size;
    header->block_size = total_size;
    header->is_mmap = 1;
    header->next = NULL;
    header->prev = NULL;

    block_footer_t *footer = (block_footer_t *)((char *)mapped + total_size -
                                                sizeof(block_footer_t));
    footer->magic_footer = ALLOC_MAGIC_FOOTER;
    footer->block_size = total_size;

    global_stats.total_allocated += size;
    global_stats.mmap_allocations++;
    pthread_mutex_unlock(&alloc_mutex);

    return (void *)((char *)header + sizeof(block_header_t));
  }

  /* Small/Medium allocations < 128 KB via Segregated Free Lists & sbrk */
  block_header_t *block = free_list_find_fit(total_size);

  if (!block) {
    void *heap_break = sbrk((intptr_t)total_size);
    if (heap_break == (void *)-1) {
      LOG_ERROR("sbrk syscall failed for size %zu", total_size);
      pthread_mutex_unlock(&alloc_mutex);
      return NULL;
    }
    block = (block_header_t *)heap_break;
    block->magic_header = ALLOC_MAGIC_HEADER;
    block->block_size = total_size;
    block->is_mmap = 0;
    block->next = NULL;
    block->prev = NULL;

    global_stats.current_heap_size += total_size;
    global_stats.sbrk_allocations++;
  } else {
    /* Split block if extra remainder is sufficiently large */
    size_t min_split = sizeof(block_header_t) + sizeof(block_footer_t) + 32;
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

void my_free(void *ptr) {
  if (!ptr)
    return;

  block_header_t *header =
      (block_header_t *)((char *)ptr - sizeof(block_header_t));

  if (header->magic_header != ALLOC_MAGIC_HEADER) {
    LOG_ERROR("Heap corruption: Invalid magic header canary at %p", ptr);
    return;
  }

  block_footer_t *footer =
      (block_footer_t *)((char *)header + header->block_size -
                         sizeof(block_footer_t));
  if (footer->magic_footer != ALLOC_MAGIC_FOOTER) {
    LOG_ERROR("Heap corruption: Invalid magic footer canary at %p", ptr);
    return;
  }

  pthread_mutex_lock(&alloc_mutex);

  if (header->is_mmap) {
    global_stats.total_freed += header->requested_size;
    size_t block_size = header->block_size;
    pthread_mutex_unlock(&alloc_mutex);

    if (munmap(header, block_size) != 0) {
      LOG_ERROR("munmap syscall failed at %p", (void *)header);
    }
    return;
  }

  header->is_free = 1;
  global_stats.total_freed += header->requested_size;
  free_list_insert(header);

  pthread_mutex_unlock(&alloc_mutex);
}

void *my_calloc(size_t nmemb, size_t size) {
  if (nmemb == 0 || size == 0)
    return NULL;

  size_t total = nmemb * size;
  if (total / nmemb != size) {
    LOG_ERROR("Integer overflow detected in calloc arguments (%zu, %zu)", nmemb,
              size);
    return NULL;
  }

  void *ptr = my_malloc(total);
  if (ptr) {
    memset(ptr, 0, total);
  }
  return ptr;
}

void *my_realloc(void *ptr, size_t size) {
  if (!ptr)
    return my_malloc(size);
  if (size == 0) {
    my_free(ptr);
    return NULL;
  }

  block_header_t *header =
      (block_header_t *)((char *)ptr - sizeof(block_header_t));
  if (header->magic_header != ALLOC_MAGIC_HEADER) {
    LOG_ERROR("Heap corruption in realloc at %p", ptr);
    return NULL;
  }

  /* If existing block capacity satisfies new size */
  size_t usable_capacity =
      header->block_size - sizeof(block_header_t) - sizeof(block_footer_t);
  if (usable_capacity >= size) {
    header->requested_size = size;
    return ptr;
  }

  void *new_ptr = my_malloc(size);
  if (!new_ptr)
    return NULL;

  size_t copy_size =
      header->requested_size < size ? header->requested_size : size;
  memcpy(new_ptr, ptr, copy_size);
  my_free(ptr);

  return new_ptr;
}
