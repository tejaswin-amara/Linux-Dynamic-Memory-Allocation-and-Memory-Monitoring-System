#include "allocator.h"

/*
 * Size classes for segregated free lists:
 * Class 0: <= 32 B
 * Class 1: <= 64 B
 * Class 2: <= 128 B
 * Class 3: <= 256 B
 * Class 4: <= 512 B
 * Class 5: <= 1024 B (1 KB)
 * Class 6: <= 2048 B (2 KB)
 * Class 7: <= 4096 B (4 KB)
 * Class 8: <= 8192 B (8 KB)
 * Class 9: > 8192 B (up to MMAP_THRESHOLD)
 */
static const size_t size_class_limits[NUM_SIZE_CLASSES] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, MMAP_THRESHOLD};

static block_header_t *segregated_heads[NUM_SIZE_CLASSES] = {NULL};

int get_size_class_index(size_t size) {
  for (int i = 0; i < NUM_SIZE_CLASSES - 1; ++i) {
    if (size <= size_class_limits[i]) {
      return i;
    }
  }
  return NUM_SIZE_CLASSES - 1;
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
  int idx = get_size_class_index(block->block_size);

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
        LOG_ERROR("Heap canary corruption detected in free list at %p",
                  (void *)curr);
        return NULL;
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
