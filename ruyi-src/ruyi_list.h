#ifndef RUYI_LIST_H
#define RUYI_LIST_H

#include "ruyi_lock.h"

typedef struct ruyi_list_t ruyi_list_t;
typedef void (*ruyi_list_free_val_func)(void**);

ruyi_list_t* ruyi_list_create(ruyi_list_free_val_func); /* mt-safe */
void ruyi_list_push(ruyi_list_t*, void*); /* mt-safe */
void* ruyi_list_pop(ruyi_list_t*); /* mt-safe */
void ruyi_list_destroy(ruyi_list_t**); /* mt-unsafe, ensure list is not accessed before this function called */

#define ruyi_list_foreach_consume(list, type, logic) { \
	ruyi_list_t* __rl__ = (list); \
	ruyi_lock(&__rl__->lock); \
	ruyi_list_node_t* __n__ = __rl__->head; \
	__rl__->head = __rl__->tail = NULL; \
	ruyi_unlock(&__rl__->lock); \
	while (__n__) { \
		ruyi_list_node_t* __next__ = __n__->next; \
		(type)__VAL__ = __n__->data; \
		logic; \
		list->free_func(&__VAL__); \
		RUYI_MEM_FREE(&__n__); \
		__n__ = __next__; \
	} \
}

#endif
