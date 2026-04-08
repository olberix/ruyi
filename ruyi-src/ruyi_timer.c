#include "ruyi_timer.h"
#include "ruyi_macros.h"
#include "ruyi_malloc.h"
#include "ruyi-ds/ruyi_mpsc_list.h"

#include <stdbool.h>
#include <stdatomic.h>

#define TIMER_ENTRY_HEAP_INIT_SIZE (1 << 20) /* 8MB */
#define CANCEL_SET_INIT_SIZE ((1 << 7) - 1)

typedef struct {
	uint64_t ts_ms;
	uint64_t id;
	ruyi_timer_callback_t cb;
	ruyi_timer_userdata_free_t udf;
	void* args;
} timer_entry_t;

typedef struct {
	uint64_t id;
	bool marked;
} cancel_set_t;

typedef struct {
	_Alignas(CACHE_LINE_SIZE) _Atomic uint64_t next_timerid;
	_Alignas(CACHE_LINE_SIZE) _Atomic bool running;
	char padding[CACHE_LINE_SIZE - sizeof(_Atomic bool)];

	timer_entry_t** timer_entries;
	cancel_set_t* cancel_set;
	size_t teh_sz;
	size_t teh_len;
	size_t cs_sz;
	size_t cs_len;

	ruyi_mpsc_list_t* pending_add_list;
	ruyi_mpsc_list_t* pending_cancel_list;
} ruyi_timer_info_t;

static ruyi_timer_info_t s_timer_info;

void ruyi_timer_init()
{
	atomic_store_explicit(&s_timer_info.next_timerid, 1, memory_order_relaxed);
	
	s_timer_info.teh_sz = TIMER_ENTRY_HEAP_INIT_SIZE;
	s_timer_info.teh_len = 0;
	s_timer_info.timer_entries = RUYI_MEM_ALLOC(s_timer_info.teh_sz * sizeof(timer_entry_t*));
	memset(s_timer_info.timer_entries, 0, s_timer_info.teh_sz * sizeof(timer_entry_t*));

	s_timer_info.cs_sz = CANCEL_SET_INIT_SIZE;
	s_timer_info.cs_len = 0;
	s_timer_info.cancel_set = RUYI_MEM_ALLOC(s_timer_info.cs_sz * sizeof(cancel_set_t));
	memset(s_timer_info.cancel_set, 0, s_timer_info.cs_sz * sizeof(cancel_set_t));

	s_timer_info.pending_add_list = ruyi_mpsc_list_create(sizeof(timer_entry_t*));
	s_timer_info.pending_cancel_list = ruyi_mpsc_list_create(sizeof(uint64_t));

	atomic_store_explicit(&s_timer_info.running, true, memory_order_release);
}

// static inline void _timer_cleanup_()
// {

// }

// static inline void _add_cancel_id_()
// {
// 	uint64_t* pid;
// 	while (pid = ruyi_mpsc_list_pop(s_timer_info.pending_cancel_list)) {

// 	}
// }

// static inline bool test_and_erase(uint64_t id)
// {
// 	for (uint64_t s = id & s_timer_info.cs_sz; true; s = (s + 1) & s_timer_info.cs_sz) {
// 		uint64_t _id = s_timer_info.cancel_set[s];
// 		if (_id == 0)
// 			return false;
// 		if(_id == id){
// 			s_timer_info.cancel_set[s] = 0;
// 			return true;
// 		}
// 	}
// }

static inline void _add_timer_node_()
{

}

void* ruyi_timer_event()
{
	while (atomic_load_explicit(&s_timer_info.running, memory_order_relaxed)) {

	}

	return NULL;
}

void ruyi_timer_notify_stop()
{
	atomic_store_explicit(&s_timer_info.running, false, memory_order_relaxed);
}

static inline uint64_t _timer_spawnid_()
{
	return atomic_fetch_add(&s_timer_info.next_timerid, 1);
}

uint64_t ruyi_timer_add(uint64_t delay_ms, ruyi_timer_callback_t cb, void* args, ruyi_timer_userdata_free_t udf)
{
	RUYI_RETURN_VAL_IFUL(cb == NULL, 0);

	uint64_t id = _timer_spawnid_();
	timer_entry_t* entry = RUYI_MEM_ALLOC(sizeof(timer_entry_t));
	entry->id = id;
	entry->ts_ms = delay_ms;
	entry->cb = cb;
	entry->args = args;
	entry->udf = udf;

	ruyi_mpsc_list_push(s_timer_info.pending_add_list, &entry);

	return id;
}

void ruyi_timer_cancel(uint64_t id)
{
	ruyi_mpsc_list_push(s_timer_info.pending_cancel_list, &id);
}
