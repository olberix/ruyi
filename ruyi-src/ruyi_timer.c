#include "ruyi_timer.h"
#include "ruyi_macros.h"
#include "ruyi_malloc.h"
#include "ruyi_clock.h"
#include "ruyi-ds/ruyi_mpsc_list.h"

#include <stdbool.h>
#include <stdatomic.h>

#define RUYI_TIMER_ENTRY_HEAP_INIT_SIZE (1 << 20)
#define RUYI_TIMER_CANCEL_SET_INIT_SIZE (1 << 20)

typedef struct {
	uint64_t ts_ms;
	uint64_t id;
	ruyi_timer_callback_t cb;
	ruyi_timer_userdata_free_t udf;
	void* ud;
} timer_entry_t;

typedef struct {
	uint64_t id;
	bool marked;
} cancel_set_t;

typedef struct {
	_Alignas(RUYI_CACHELINE_SIZE) _Atomic uint64_t next_timerid;
	_Alignas(RUYI_CACHELINE_SIZE) _Atomic bool running;
	char padding[RUYI_CACHELINE_SIZE - sizeof(_Atomic bool)];

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
	memset(&s_timer_info, 0, sizeof(s_timer_info));

	atomic_store_explicit(&s_timer_info.next_timerid, 1, memory_order_relaxed);
	
	s_timer_info.teh_sz = RUYI_TIMER_ENTRY_HEAP_INIT_SIZE;
	s_timer_info.teh_len = 0;
	s_timer_info.timer_entries = RUYI_MEM_ALLOC(s_timer_info.teh_sz * sizeof(timer_entry_t*));
	memset(s_timer_info.timer_entries, 0, s_timer_info.teh_sz * sizeof(timer_entry_t*));

	s_timer_info.cs_sz = RUYI_TIMER_CANCEL_SET_INIT_SIZE;
	s_timer_info.cs_len = 0;
	s_timer_info.cancel_set = RUYI_MEM_ALLOC(s_timer_info.cs_sz * sizeof(cancel_set_t));
	memset(s_timer_info.cancel_set, 0, s_timer_info.cs_sz * sizeof(cancel_set_t));

	s_timer_info.pending_add_list = ruyi_mpsc_list_create(sizeof(timer_entry_t*));
	s_timer_info.pending_cancel_list = ruyi_mpsc_list_create(sizeof(uint64_t));

	atomic_store_explicit(&s_timer_info.running, true, memory_order_release);
}

static void _entry_free_(void* pval)
{
	timer_entry_t* entry = *((timer_entry_t**)pval);
	RUYI_MEM_FREE(&entry);
}

static inline void _timer_cleanup_()
{
	for (size_t i = 0; i < s_timer_info.teh_len; i++) {
		timer_entry_t* entry = s_timer_info.timer_entries[i];
		entry->udf(entry->ud);
		RUYI_MEM_FREE(&entry);
	}
	RUYI_MEM_FREE(&s_timer_info.timer_entries);

	RUYI_MEM_FREE(&s_timer_info.cancel_set);

	struct timespec ts = {.tv_sec = 0, .tv_nsec = 300000000}; /* 300ms */
	nanosleep(&ts, NULL);
	ruyi_mpsc_list_destroy(&s_timer_info.pending_cancel_list, _entry_free_);
	ruyi_mpsc_list_destroy(&s_timer_info.pending_add_list, NULL);
}

static inline uint64_t _hash_(uint64_t id)
{
	return id & (s_timer_info.cs_sz - 1);
}

static inline void _add_cancel_id_(uint64_t id)
{
	if (ruyi_unlikely(s_timer_info.cs_len > (s_timer_info.cs_sz >> 1))) {
		cancel_set_t* old_set = s_timer_info.cancel_set;
		size_t old_sz = s_timer_info.cs_sz;
		s_timer_info.cs_sz = (old_sz << 1);
		s_timer_info.cs_len = 0;
		s_timer_info.cancel_set = RUYI_MEM_ALLOC(s_timer_info.cs_sz * sizeof(cancel_set_t));
		for (size_t i = 0; i < old_sz; i++) {
			if (old_set[i].id != 0) {
				_add_cancel_id_(old_set[i].id);
			}
		}
		RUYI_MEM_FREE(&old_set);
	}

	uint64_t s = _hash_(id);
	if (s_timer_info.cancel_set[s].id == 0 && s_timer_info.cancel_set[s].marked == false) {
		s_timer_info.cancel_set[s].id = id;
	}
	else {
		while (true) {
			s_timer_info.cancel_set[s].marked = true;
			if (s_timer_info.cancel_set[s].id == 0) {
				s_timer_info.cancel_set[s].id = id;
				break;
			}
			s = _hash_(s + 1);
		}
	}
	s_timer_info.cs_len++;
}

static inline void _fetch_cancel_pending_()
{
	uint64_t* p;
	while ((p = ruyi_mpsc_list_pop(s_timer_info.pending_cancel_list)) != NULL) {
		_add_cancel_id_(*p);
		RUYI_MEM_FREE(&p);
	}
}

static inline void _add_timer_entry_(timer_entry_t* entry)
{
	if(ruyi_unlikely(s_timer_info.teh_len >= s_timer_info.teh_sz)) {
		s_timer_info.teh_sz <<= 1;
		s_timer_info.timer_entries = RUYI_MEM_REALLOC(s_timer_info.timer_entries, s_timer_info.teh_sz * sizeof(timer_entry_t*));
	}

	size_t s = s_timer_info.teh_len++;
	s_timer_info.timer_entries[s] = entry;
	while (s != 0) {
		size_t sp = ((s - 1) >> 1);
		if (s_timer_info.timer_entries[sp]->ts_ms <= s_timer_info.timer_entries[s]->ts_ms) {
			break;
		}

		timer_entry_t* tmp = s_timer_info.timer_entries[sp];
		s_timer_info.timer_entries[sp] = s_timer_info.timer_entries[s];
		s_timer_info.timer_entries[s] = tmp;
		
		s = sp;
	}
}

static inline void _fetch_add_pending_()
{
	timer_entry_t** pe;
	while ((pe = ruyi_mpsc_list_pop(s_timer_info.pending_add_list)) != NULL) {
		_add_timer_entry_(*pe);
		RUYI_MEM_FREE(&pe);
	}
}

static inline bool _test_and_erase_(uint64_t id)
{
	for (uint64_t s = _hash_(id); true; s = _hash_(s + 1)) {
		if (s_timer_info.cancel_set[s].id == id) {
			s_timer_info.cancel_set[s].id = 0;
			if (s_timer_info.cancel_set[s].marked == true && s_timer_info.cancel_set[_hash_(s + 1)].marked == false) {
				do {
					s_timer_info.cancel_set[s].marked = false;
					if (ruyi_likely(s != 0)) {
						s = _hash_(s - 1);
					}
					else {
						s = _hash_(s_timer_info.cs_sz - 1);
					}
				}while (s_timer_info.cancel_set[s].id == 0 && s_timer_info.cancel_set[s].marked == true);
			}
			return true;
		}
		RUYI_RETURN_VAL_IF(s_timer_info.cancel_set[s].marked == false, false);
	}
}

static inline timer_entry_t* _t_pop_()
{
	timer_entry_t* entry = s_timer_info.timer_entries[0];
	--s_timer_info.teh_len;
	s_timer_info.timer_entries[0] = s_timer_info.timer_entries[s_timer_info.teh_len];

	size_t s = 0;
	while (s < s_timer_info.teh_len) {
		size_t sl = (s << 1) + 1;
		if (sl > s_timer_info.teh_len) {
			break;
		}
		size_t rpl = sl, sr = sl + 1;
		if (sr < s_timer_info.teh_len && s_timer_info.timer_entries[sr]->ts_ms < s_timer_info.timer_entries[sl]->ts_ms) {
			rpl = sr;
		}
		if (s_timer_info.timer_entries[s]->ts_ms <= s_timer_info.timer_entries[rpl]->ts_ms) {
			break;
		}

		timer_entry_t* tmp = s_timer_info.timer_entries[s];
		s_timer_info.timer_entries[s] = s_timer_info.timer_entries[rpl];
		s_timer_info.timer_entries[rpl] = tmp;

		s = rpl;
	}

	return entry;
}

void* ruyi_timer_event()
{
	static struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000}; /* 1ms */
	
	while (atomic_load_explicit(&s_timer_info.running, memory_order_relaxed)) {
		_fetch_cancel_pending_();
		_fetch_add_pending_();

		uint64_t cur_ts = ruyi_clock_time_ms();
		if(s_timer_info.teh_len == 0 || s_timer_info.timer_entries[0]->ts_ms > cur_ts) {
			nanosleep(&ts, NULL);
			continue;
		}

		do {
			timer_entry_t* entry = _t_pop_();
			if (_test_and_erase_(entry->id) == false) {
				entry->cb(entry->ud);
			}
			entry->udf(entry->ud);
			_entry_free_(&entry);
		} while (s_timer_info.teh_len != 0 && s_timer_info.timer_entries[0]->ts_ms <= cur_ts);
	}

	_timer_cleanup_();

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

uint64_t ruyi_timer_add(uint64_t delay_ms, ruyi_timer_callback_t cb, void* ud, ruyi_timer_userdata_free_t udf)
{
	RUYI_RETURN_VAL_IFUL(cb == NULL || udf == NULL || atomic_load_explicit(&s_timer_info.running, memory_order_relaxed) == false, 0);

	uint64_t id = _timer_spawnid_();
	timer_entry_t* entry = RUYI_MEM_ALLOC(sizeof(timer_entry_t));
	entry->id = id;
	entry->ts_ms = delay_ms + ruyi_clock_time_ms();
	entry->cb = cb;
	entry->ud = ud;
	entry->udf = udf;

	ruyi_mpsc_list_push(s_timer_info.pending_add_list, &entry);

	return id;
}

void ruyi_timer_cancel(uint64_t id)
{
	RUYI_RETURN_IFUL(atomic_load_explicit(&s_timer_info.running, memory_order_relaxed) == false);

	ruyi_mpsc_list_push(s_timer_info.pending_cancel_list, &id);
}
