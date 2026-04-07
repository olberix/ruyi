#include "ruyi_mpsc_list.h"
#include "ruyi_def.h"
#include "ruyi_check.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

typedef struct ruyi_mpsc_list_node_t {
	_Alignas(CACHE_LINE_SIZE) _Atomic struct ruyi_mpsc_list_node_t* next;
	char padding[CACHE_LINE_SIZE - sizeof(_Atomic struct ruyi_mpsc_list_node_t*)];

	void* pval;
} ruyi_mpsc_list_node_t;

struct ruyi_mpsc_list_t {
	_Alignas(CACHE_LINE_SIZE) _Atomic ruyi_mpsc_list_node_t* head;
	_Alignas(CACHE_LINE_SIZE) _Atomic ruyi_mpsc_list_node_t* tail;
	char padding[CACHE_LINE_SIZE - sizeof(_Atomic ruyi_mpsc_list_node_t*)];

	size_t sz;
};

ruyi_mpsc_list_t *ruyi_mpsc_list_create(size_t sz)
{
	RUYI_EXIT_IF_MSG(sz == 0, "ruyi_mpsc_list_create(): sz = 0\n");

	ruyi_mpsc_list_t* list = RUYI_MEM_ALLOC(sizeof(ruyi_mpsc_list_t));
	list->sz = sz;
	ruyi_mpsc_list_node_t* dummy = RUYI_MEM_ALLOC(sizeof(ruyi_mpsc_list_node_t));
	atomic_store_explicit(&dummy->next, NULL, memory_order_relaxed);
	atomic_store_explicit(&list->head, (_Atomic ruyi_mpsc_list_node_t*)dummy, memory_order_relaxed);
	atomic_store_explicit(&list->tail, (_Atomic ruyi_mpsc_list_node_t*)dummy, memory_order_relaxed);

	atomic_thread_fence(memory_order_release);
	return list;
}

void ruyi_mpsc_list_push(ruyi_mpsc_list_t* list, void* pval)
{
	RUYI_RETURN_IF(list == NULL || pval == NULL);

	ruyi_mpsc_list_node_t* node = RUYI_MEM_ALLOC(sizeof(ruyi_mpsc_list_node_t));
	node->pval = RUYI_MEM_ALLOC(list->sz);
	memcpy(node->pval, pval, list->sz);
	atomic_store_explicit(&node->next, NULL, memory_order_relaxed);

	ruyi_mpsc_list_node_t* ot = (ruyi_mpsc_list_node_t*)atomic_exchange_explicit(&list->tail, (_Atomic ruyi_mpsc_list_node_t*)node, memory_order_relaxed);
	atomic_store_explicit(&ot->next, (_Atomic ruyi_mpsc_list_node_t*)node, memory_order_release);
}

void* ruyi_mpsc_list_pop(ruyi_mpsc_list_t* list)
{
	RUYI_RETURN_VAL_IF(list == NULL, NULL);

	ruyi_mpsc_list_node_t* h = (ruyi_mpsc_list_node_t*)atomic_load_explicit(&list->head, memory_order_relaxed);
	ruyi_mpsc_list_node_t* nh = (ruyi_mpsc_list_node_t*)atomic_load_explicit(&h->next, memory_order_relaxed);
	RUYI_RETURN_VAL_IF(nh == NULL, NULL);

	void* pval = nh->pval;
	atomic_store_explicit(&list->head, (_Atomic ruyi_mpsc_list_node_t*)nh, memory_order_relaxed);
	RUYI_MEM_FREE(&h);

	return pval;
}

void ruyi_mpsc_list_destroy(ruyi_mpsc_list_t** plist)
{
	RUYI_RETURN_IF(plist == NULL);
	ruyi_mpsc_list_t* list = *plist;
	RUYI_RETURN_IF(list == NULL);
	
	ruyi_mpsc_list_node_t* h = (ruyi_mpsc_list_node_t*)atomic_load_explicit(&list->head, memory_order_acquire);
	ruyi_mpsc_list_node_t* next = (ruyi_mpsc_list_node_t*)atomic_load_explicit(&h->next, memory_order_acquire);
	while (next) {
		ruyi_mpsc_list_node_t* tmp = next;
		next = (ruyi_mpsc_list_node_t*)atomic_load_explicit(&next->next, memory_order_acquire);
		RUYI_MEM_FREE(&tmp->pval);
		RUYI_MEM_FREE(&tmp);
	}
	RUYI_MEM_FREE(&h);
	RUYI_MEM_FREE(plist);
}
