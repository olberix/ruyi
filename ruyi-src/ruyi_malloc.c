#include "ruyi_malloc.h"
#include "ruyi_check.h"

#include <stdlib.h>

struct ruyi_malloc_t {
    void* (*ruyi_alloc_func)(size_t, void*);
    void* (*ruyi_realloc_func)(void*, size_t, void*);
    void* (*ruyi_free_func)(void*, void*);

    void* context;
};

static inline void* default_alloc(size_t sz, void* context)
{
    return malloc(sz);
}

static inline void* default_realloc(void* ptr, size_t sz, void* context)
{
    return realloc(ptr, sz);
}

static inline void* default_free(void* ptr, void* context)
{
    free(ptr);
}

static ruyi_malloc_t s_mem_allocator = {
    .ruyi_alloc_func = default_alloc,
    .ruyi_realloc_func = default_realloc,
    .ruyi_free_func = default_free,
    
    .context = NULL
};

void ruyi_mem_alloc_init(const ruyi_malloc_t *ma)
{
    RUYI_RETURN_IF(ma == NULL);
    s_mem_allocator = *ma;
}

void ruyi_mem_alloc_destroy()
{
    free(s_mem_allocator.context);
}

void* ruyi_mem_alloc(size_t size)
{
  return s_mem_allocator.ruyi_alloc_func(size, s_mem_allocator.context);
}

void* ruyi_mem_realloc(void* ptr, size_t size)
{
    return s_mem_allocator.ruyi_realloc_func(ptr, size, s_mem_allocator.context);
}

void ruyi_mem_free(void** pptr)
{
    s_mem_allocator.ruyi_free_func(*pptr, s_mem_allocator.context);
    *pptr = NULL;
}
