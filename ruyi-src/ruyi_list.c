#include "ruyi_list.h"
#include "ruyi_check.h"

typedef struct ruyi_list_node_t {
	void* data;
	struct ruyi_list_node_t* next;
} ruyi_list_node_t;

struct ruyi_list_t {
	ruyi_lock_t lock;
	ruyi_list_free_val_func free_func;

	ruyi_list_node_t* head;
	ruyi_list_node_t* tail;
};

void default_free_val_func(void** pptr)
{
	((void)0);
}

ruyi_list_t* ruyi_list_create(ruyi_list_free_val_func func)
{
	ruyi_list_t* list = RUYI_MEM_ALLOC(sizeof(ruyi_list_t));
	ruyi_lock_init(&list->lock);
	if (func) {
		list->free_func = func;
	}
	else {
		list->free_func = default_free_val_func;
	}
	list->head = NULL;
	list->tail = NULL;

	return list;
}

void ruyi_list_push(ruyi_list_t* list, void* val)
{
	RUYI_RETURN_IF(list == NULL);

	ruyi_list_node_t* node = RUYI_MEM_ALLOC(sizeof(ruyi_list_node_t));
	node->data = val;
	node->next = NULL;

	ruyi_lock(&list->lock);
	list->tail->next = node;
	if(list->head == NULL) {
		list->head = node;
	}
	list->tail = node;
	ruyi_unlock(&list->lock);
}

void* ruyi_list_pop(ruyi_list_t* list)
{
	RUYI_RETURN_VAL_IF(list == NULL, NULL);

	ruyi_lock(&list->lock);
	if(list->head == NULL) {
		ruyi_unlock(&list->lock);
		return NULL;
	}
	ruyi_list_node_t* node = list->head;
	list->head = node->next;
	ruyi_unlock(&list->lock);

	void* val = node->data;
	RUYI_MEM_FREE(&node);
	return val;
}

void ruyi_list_destroy(ruyi_list_t** plist)
{
	ruyi_list_t* list = *plist;
	for(ruyi_list_node_t* node = list->head; node != NULL;) {
		ruyi_list_node_t* next = node->next;
		list->free_func(&node->data);
		RUYI_MEM_FREE(&node);
		node = next;
	}
	ruyi_lock_destroy(&list->lock);
	*plist = NULL;
}
