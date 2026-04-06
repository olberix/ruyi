#include "ruyi_spsc_list.h"
#include "ruyi_def.h"
#include "ruyi_check.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

typedef struct ruyi_spsc_list_node_t {
	_Atomic struct ruyi_spsc_list_node_t* next;
	char padding[CACHE_LINE_SIZE - sizeof(_Atomic struct ruyi_spsc_list_node_t*)];
	
	void* pval;
} ruyi_spsc_list_node_t;

struct ruyi_spsc_list_t {
	ruyi_spsc_list_node_t* head;
	ruyi_spsc_list_node_t* tail;

	size_t sz;
};

ruyi_spsc_list_t *ruyi_spsc_list_create(size_t sz)
{
	ruyi_spsc_list_t* list = RUYI_MEM_ALLOC(sizeof(ruyi_spsc_list_t));
	list->sz = sz;
	ruyi_spsc_list_node_t* dummy = RUYI_MEM_ALLOC(sizeof(ruyi_spsc_list_node_t));
	atomic_store_explicit(&dummy->next, NULL, memory_order_relaxed);
	atomic_store_explicit(&list->head, (void*)dummy, memory_order_relaxed);
	atomic_store_explicit(&list->tail, (void*)dummy, memory_order_relaxed);

	return list;
}

void ruyi_spsc_list_push(ruyi_spsc_list_t* list, void* pval)
{
	RUYI_RETURN_IF(list == NULL || pval == NULL);

	ruyi_spsc_list_node_t* node = RUYI_MEM_ALLOC(sizeof(ruyi_spsc_list_node_t));
	node->pval = RUYI_MEM_ALLOC(list->sz);
	memcpy(node->pval, pval, list->sz);
	atomic_store_explicit(&node->next, NULL, memory_order_relaxed);

	ruyi_spsc_list_node_t* t = list->tail;
	list->tail = node;
	atomic_store_explicit(&t->next, (void*)node, memory_order_release);
}

void* ruyi_spsc_list_pop(ruyi_spsc_list_t* list)
{
	RUYI_RETURN_VAL_IF(list == NULL, NULL);

	ruyi_spsc_list_node_t* nh = (ruyi_spsc_list_node_t*)atomic_load_explicit(&list->head->next, memory_order_relaxed);
	RUYI_RETURN_VAL_IF(nh == NULL, NULL);

	void* pval = nh->pval;
	RUYI_MEM_FREE(&list->head);
	list->head = nh;

	return pval;
}

void ruyi_spsc_list_destroy(ruyi_spsc_list_t** plist)
{
	RUYI_RETURN_IF(plist == NULL);
	ruyi_spsc_list_t* list = *plist;
	RUYI_RETURN_IF(list == NULL);
	
	ruyi_spsc_list_node_t* next = (ruyi_spsc_list_node_t*)atomic_load_explicit(&list->head->next, memory_order_acquire);
	while (next) {
		ruyi_spsc_list_node_t* tmp = next;
		next = (ruyi_spsc_list_node_t*)atomic_load_explicit(&next->next, memory_order_acquire);
		RUYI_MEM_FREE(&tmp->pval);
		RUYI_MEM_FREE(&tmp);
	}
	RUYI_MEM_FREE(&list->head);
	RUYI_MEM_FREE(plist);
}
