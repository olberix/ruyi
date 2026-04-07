#include "ruyi_spmc_list.h"
#include "ruyi_def.h"
#include "ruyi_check.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

typedef struct ruyi_spmc_list_node_t {
	_Alignas(CACHE_LINE_SIZE) _Atomic struct ruyi_spmc_list_node_t* next;
	char padding[CACHE_LINE_SIZE - sizeof(_Atomic struct ruyi_mpsc_list_node_t*)];

	void* pval;
} ruyi_spmc_list_node_t;

struct ruyi_spmc_list_t {
	_Alignas(CACHE_LINE_SIZE) _Atomic ruyi_spmc_list_node_t* head;
	_Alignas(CACHE_LINE_SIZE) _Atomic ruyi_spmc_list_node_t* tail;
	char padding[CACHE_LINE_SIZE - sizeof(_Atomic ruyi_spmc_list_node_t*)];

	size_t sz;
};

ruyi_spmc_list_t *ruyi_spmc_list_create(size_t sz)
{
	RUYI_EXIT_IF_MSG(sz == 0, "ruyi_spmc_list_create(): sz = 0\n");

	ruyi_spmc_list_t* list = RUYI_MEM_ALLOC(sizeof(ruyi_spmc_list_t));
	list->sz = sz;
	ruyi_spmc_list_node_t* dummy = RUYI_MEM_ALLOC(sizeof(ruyi_spmc_list_node_t));
	atomic_store_explicit(&dummy->next, NULL, memory_order_relaxed);
	atomic_store_explicit(&list->head, (_Atomic ruyi_spmc_list_node_t*)dummy, memory_order_relaxed);
	atomic_store_explicit(&list->tail, (_Atomic ruyi_spmc_list_node_t*)dummy, memory_order_relaxed);

	atomic_thread_fence(memory_order_release);
	return list;
}

void ruyi_spmc_list_push(ruyi_spmc_list_t* list, void* pval)
{
	RUYI_RETURN_IF(list == NULL || pval == NULL);

	ruyi_spmc_list_node_t* node = RUYI_MEM_ALLOC(sizeof(ruyi_spmc_list_node_t));
	atomic_store_explicit(&node->next, NULL, memory_order_relaxed);

	ruyi_spmc_list_node_t* t = (ruyi_spmc_list_node_t*)atomic_load_explicit(&list->tail, memory_order_relaxed);
	t->pval = RUYI_MEM_ALLOC(list->sz);
	memcpy(t->pval, pval, list->sz);
	atomic_store_explicit(&list->tail, (_Atomic ruyi_spmc_list_node_t*)node, memory_order_relaxed);
	atomic_store_explicit(&t->next, (_Atomic ruyi_spmc_list_node_t*)node, memory_order_release);
}

void* ruyi_spmc_list_pop(ruyi_spmc_list_t* list)
{
	RUYI_RETURN_VAL_IF(list == NULL, NULL);

	ruyi_spmc_list_node_t* h = (ruyi_spmc_list_node_t*)atomic_load_explicit(&list->head, memory_order_relaxed);
	while (true) {
		ruyi_spmc_list_node_t* t = (ruyi_spmc_list_node_t*)atomic_load_explicit(&list->tail, memory_order_relaxed);
		if(h == t) {
			return NULL;
		}
		ruyi_spmc_list_node_t* nh = (ruyi_spmc_list_node_t*)atomic_load_explicit(&h->next, memory_order_relaxed);
		if (nh == NULL) {
			return NULL;
		}
		if(atomic_compare_exchange_strong_explicit(&list->head, (_Atomic ruyi_spmc_list_node_t**)&h, (_Atomic ruyi_spmc_list_node_t*)nh, memory_order_relaxed, memory_order_relaxed)) {
			void* pval = h->pval;
			RUYI_MEM_FREE(&h);
			return pval;
		}
	}
}

void ruyi_spmc_list_destroy(ruyi_spmc_list_t** plist)
{
	RUYI_RETURN_IF(plist == NULL);
	ruyi_spmc_list_t* list = *plist;
	RUYI_RETURN_IF(list == NULL);
	
	ruyi_spmc_list_node_t* h = (ruyi_spmc_list_node_t*)atomic_load_explicit(&list->head, memory_order_acquire);
	ruyi_spmc_list_node_t* t = (ruyi_spmc_list_node_t*)atomic_load_explicit(&list->tail, memory_order_acquire);
	while (h != t) {
		ruyi_spmc_list_node_t* tmp = h;
		h = (ruyi_spmc_list_node_t*)atomic_load_explicit(&h->next, memory_order_acquire);
		RUYI_MEM_FREE(&tmp->pval);
		RUYI_MEM_FREE(&tmp);
	}
	RUYI_MEM_FREE(&t);
	RUYI_MEM_FREE(plist);
}
