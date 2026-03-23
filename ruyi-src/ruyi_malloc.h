#ifndef RUYI_ALLOCATOR_H
#define RUYI_ALLOCATOR_H

#include <stddef.h>

typedef struct {
    void* (*ruyi_alloc_func)(size_t, void*);
    void* (*ruyi_realloc_func)(void*, size_t, void*);
    void* (*ruyi_free_func)(size_t, void*);

    void* context;
} ruyi_allocator_t;

void ruyi_mem_alloc_init(const ruyi_allocator_t* ma);

void* ruyi_mem_alloc(size_t size);
void* ruyi_mem_realloc(void* ptr, size_t size);
void ruyi_mem_free(void* ptr);

#define RUYI_MEM_ALLOC(sz) ruyi_mem_alloc(sz)
#define RUYI_MEM_REALLOC(ptr, sz) ruyi_mem_realloc(ptr, sz)
#define RUYI_MEM_FREE(ptr) ruyi_mem_free(ptr)

#endif
