#ifndef RUYI_ALLOCATOR_H
#define RUYI_ALLOCATOR_H

#include <stddef.h>

typedef struct {
	void* (*ruyi_alloc_func)(size_t, void*);
	void* (*ruyi_realloc_func)(void*, size_t, void*);
	void (*ruyi_free_func)(void*, void*);

	void* context;
} ruyi_malloc_t;

/* invoke this function once during early startup, or omit it entirely. ma->context should be allocated by malloc in Clang */
void ruyi_mem_alloc_init(const ruyi_malloc_t* ma);
/* this function must be invoked once at program exit if ruyi_mem_alloc_init was previously called */
void ruyi_mem_alloc_destroy();

void* ruyi_mem_alloc(size_t size);
void* ruyi_mem_realloc(void* ptr, size_t size);
void ruyi_mem_free(void** pptr);

#define RUYI_MEM_ALLOC(sz) ruyi_mem_alloc(sz)
#define RUYI_MEM_REALLOC(ptr, sz) ruyi_mem_realloc(ptr, sz)
#define RUYI_MEM_FREE(pptr) ruyi_mem_free((void**)(pptr))

#endif
