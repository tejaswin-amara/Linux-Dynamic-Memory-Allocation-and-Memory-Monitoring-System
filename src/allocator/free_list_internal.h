#ifndef FREE_LIST_INTERNAL_H
#define FREE_LIST_INTERNAL_H

#include "allocator.h"

__attribute__((visibility("internal"))) void free_list_reset(void);
__attribute__((visibility("internal"))) void free_list_insert(block_header_t *block);
__attribute__((visibility("internal"))) void free_list_remove(block_header_t *block);
__attribute__((visibility("internal"))) block_header_t *free_list_find_fit(size_t total_size);
__attribute__((visibility("internal"))) int free_list_verify_integrity(void);

#endif /* FREE_LIST_INTERNAL_H */
