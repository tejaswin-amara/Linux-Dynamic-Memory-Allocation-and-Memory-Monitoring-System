static void fatal_abort(const char *msg) {
  ssize_t ret = write(STDERR_FILENO, msg, strlen(msg));
  (void)ret;
  abort();
}
#include "allocator.h"
#include "free_list_internal.h"
#include <unistd.h>

static const size_t size_class_limits[NUM_SIZE_CLASSES] = {
    128, 256, 512, 1024, 2048, 4096, 8192, MMAP_THRESHOLD};

static block_header_t *segregated_heads[NUM_SIZE_CLASSES] = {NULL};

static int get_size_class_index(size_t block_size) {
  for (int i = 0; i < NUM_SIZE_CLASSES - 1; ++i) {
    if (block_size <= size_class_limits[i]) {
      return i;
    }
  }
  return NUM_SIZE_CLASSES - 1;
}

void free_list_reset(void) {
  for (int i = 0; i < NUM_SIZE_CLASSES; ++i) {
    segregated_heads[i] = NULL;
  }
}

void free_list_insert(block_header_t *block) {
  if (!block)
    return;
  int idx = get_size_class_index(block->block_size);

  block->next = segregated_heads[idx];
  block->prev = NULL;
  if (segregated_heads[idx]) {
    segregated_heads[idx]->prev = block;
  }
  segregated_heads[idx] = block;
  block->is_free = 1;
}

void free_list_remove(block_header_t *block) {
  if (!block)
    return;
  if (block->block_size < sizeof(block_header_t) + sizeof(block_footer_t)) {
    fatal_abort(
        "[FATAL] Invalid block size while unlinking free-list block\n");
  }
  if (!block->is_free) {
    fatal_abort("[FATAL] Attempt to unlink an allocated block\n");
  }

  int idx = get_size_class_index(block->block_size);
  if (!block->prev && segregated_heads[idx] != block) {
    fatal_abort("[FATAL] Free-list head linkage corruption detected\n");
  }
  if (block->prev && block->prev->next != block) {
    fatal_abort("[FATAL] Free-list previous linkage corruption detected\n");
  }
  if (block->next && block->next->prev != block) {
    fatal_abort("[FATAL] Free-list next linkage corruption detected\n");
  }

  block_footer_t *footer =
      (block_footer_t *)((char *)block + block->block_size -
                         sizeof(block_footer_t));
  if (footer->magic_footer != ALLOC_MAGIC_FOOTER ||
      footer->block_size != block->block_size) {
    fatal_abort("[FATAL] Free-list footer corruption detected\n");
  }

  if (block->prev) {
    block->prev->next = block->next;
  } else {
    segregated_heads[idx] = block->next;
  }

  if (block->next) {
    block->next->prev = block->prev;
  }

  block->next = NULL;
  block->prev = NULL;
  block->is_free = 0;
}

block_header_t *free_list_find_fit(size_t total_size) {
  int start_idx = get_size_class_index(total_size);

  for (int i = start_idx; i < NUM_SIZE_CLASSES; ++i) {
    block_header_t *curr = segregated_heads[i];
    block_header_t *best_fit = NULL;
    size_t min_diff = (size_t)-1;

    while (curr) {
      if (curr->magic_header != ALLOC_MAGIC_HEADER) {
        fatal_abort(
            "[FATAL] Heap canary corruption detected in free_list_find_fit\n");
      }
      if (curr->block_size >= total_size) {
        size_t diff = curr->block_size - total_size;
        if (diff < min_diff) {
          min_diff = diff;
          best_fit = curr;
          if (diff == 0)
            break; /* Exact fit */
        }
      }
      curr = curr->next;
    }

    if (best_fit) {
      free_list_remove(best_fit);
      return best_fit;
    }
  }
  return NULL;
}

int free_list_verify_integrity(void) {
  for (int i = 0; i < NUM_SIZE_CLASSES; ++i) {
    block_header_t *curr = segregated_heads[i];
    while (curr) {
      if (curr->magic_header != ALLOC_MAGIC_HEADER) {
        return -1;
      }
      if (!curr->is_free) {
        return -1;
      }
      block_footer_t *footer =
          (block_footer_t *)((char *)curr + curr->block_size -
                             sizeof(block_footer_t));
      if (footer->magic_footer != ALLOC_MAGIC_FOOTER ||
          footer->block_size != curr->block_size) {
        return -1;
      }
      curr = curr->next;
    }
  }
  return 0;
}
