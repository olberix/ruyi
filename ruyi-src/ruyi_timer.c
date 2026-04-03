// #include "ruyi_timer.h"
// #include "ruyi_def.h"
// #include "ruyi_list.h"

// #include <stdbool.h>
// #include <stdatomic.h>

// typedef struct {
// 	uint64_t ms_ts;
// 	ruyi_timer_callback_func cb;
// 	void* args;
// } ruyi_timer_node;

// typedef struct {
// 	_Alignas(CACHE_LINE_SIZE) _Atomic uint64_t next_timerid;
// 	_Atomic bool running;

// 	ruyi_list_t* add_list;
// 	ruyi_list_t* cancel_list;
// } ruyi_timer_info_t;

// static ruyi_timer_info_t s_timer_info;

// void ruyi_timer_init()
// {
// 	atomic_init(&s_timer_info.next_timerid, 1);
// 	s_timer_info.add_list = ruyi_list_create(NULL);
// 	s_timer_info.cancel_list = ruyi_list_create(NULL);
// 	atomic_store_explicit(&s_timer_info.running, true, memory_order_release);
// }

// static inline void _timer_cleanup_()
// {

// }

// void* ruyi_timer_event(void* args)
// {

// }

// void ruyi_timer_notify_stop()
// {
// 	atomic_store_explicit(&s_timer_info.running, false, memory_order_relaxed);
// }

// static inline uint64_t _timer_spawnid_()
// {
// 	return atomic_fetch_add(&s_timer_info.next_timerid, 1);
// }

// uint64_t ruyi_timer_add(uint64_t delay_ms, ruyi_timer_callback_func cb, void* args, size_t sz)
// {
// 	return 0;
// }
