#ifndef RUYI_ALLOCATOR_H
#define RUYI_ALLOCATOR_H

#include <stddef.h>

typedef struct ruyi_malloc_t ruyi_malloc_t;

void ruyi_mem_alloc_init(const ruyi_malloc_t* ma);

void* ruyi_mem_alloc(size_t size);
void* ruyi_mem_realloc(void* ptr, size_t size);
void ruyi_mem_free(void* ptr);

#define RUYI_MEM_ALLOC(sz) ruyi_mem_alloc(sz)
#define RUYI_MEM_REALLOC(ptr, sz) ruyi_mem_realloc(ptr, sz)
#define RUYI_MEM_FREE(ptr) ruyi_mem_free(ptr)

#endif
