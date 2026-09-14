#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "common.h"

#define ALLOC_MAGIC_HEADER 0xDEADBEEF
#define ALLOC_MAGIC_FOOTER 0xBEEFDEAD
#define MMAP_THRESHOLD (128 * 1024) /* 128 KB */

#define NUM_SIZE_CLASSES 10

/*
 * Block header structure embedded immediately before the user payload.
 * Size is padded to maintain 16-byte alignment.
 */
typedef struct block_header {
  uint32_t magic_header;     /* Canary: 0xDEADBEEF */
  uint32_t is_free;          /* 1 if free, 0 if in use */
  size_t requested_size;     /* Size requested by caller */
  size_t block_size;         /* Total size including headers/footers */
  uint32_t is_mmap;          /* 1 if allocated via mmap, 0 if sbrk */
  uint32_t padding;          /* Padding to guarantee 16-byte alignment */
  struct block_header *next; /* Next free block in segregated list */
  struct block_header *prev; /* Prev free block in segregated list */
} block_header_t;

/* Block footer structure placed at the tail of the block for coalescing */
typedef struct block_footer {
  uint32_t magic_footer; /* Canary: 0xBEEFDEAD */
  uint32_t padding;
  size_t block_size;
} block_footer_t;

/* Allocator statistics */
typedef struct {
  size_t total_allocated;
  size_t total_freed;
  size_t current_heap_size;
  size_t mmap_allocations;
  size_t sbrk_allocations;
} allocator_stats_t;

/* Public Allocator API */
void *my_malloc(size_t size);
void my_free(void *ptr);
void *my_calloc(size_t nmemb, size_t size);
void *my_realloc(void *ptr, size_t size);

/* Inspection & Maintenance */
void allocator_init(void);
void allocator_destroy(void);
allocator_stats_t allocator_get_stats(void);
int allocator_verify_integrity(void);

/* Segregated Free List internals */
int get_size_class_index(size_t size);
void free_list_insert(block_header_t *block);
void free_list_remove(block_header_t *block);
block_header_t *free_list_find_fit(size_t total_size);

#endif /* ALLOCATOR_H */
