#ifndef RUYI_LOCK_H
#define RUYI_LOCK_H

#include "ruyi_def.h"

#define RUYI_LOCK_OK 1
#define RUYI_LOCK_BUSY 2

#ifdef RUYI_LOCK_FREE

	#include <stdatomic.h>

	#if defined(__x86_64__)
		#define RUYI_CPU_PAUSE() __asm__ volatile("pause" ::: "memory")
	#elif defined(__aarch64__)
		#define RUYI_CPU_PAUSE() __asm__ volatile("yield" ::: "memory")
	#else
		#define RUYI_CPU_PAUSE() atomic_thread_fence(memory_order_seq_cst)
	#endif

	typedef struct {
		_Alignas(CACHE_LINE_SIZE) atomic_flag lock;
		char padding[CACHE_LINE_SIZE - sizeof(_Alignas(CACHE_LINE_SIZE) atomic_flag)];
	} ruyi_lock_t;

	static inline void ruyi_lock_init(ruyi_lock_t* lk)
	{
		atomic_flag_clear_explicit(&lk->lock, memory_order_relaxed);
	}

	static inline void ruyi_lock(ruyi_lock_t* lk)
	{
		while (atomic_flag_test_and_set_explicit(&lk->lock, memory_order_acquire)) {
			RUYI_CPU_PAUSE();
		}
	}

	static inline int ruyi_trylock(ruyi_lock_t* lk)
	{
		if (!atomic_flag_test_and_set_explicit(&lk->lock, memory_order_acquire)) {
			return RUYI_LOCK_OK;
		}
		return RUYI_LOCK_BUSY;
	}

	static inline void ruyi_unlock(ruyi_lock_t* lk)
	{
		atomic_flag_clear_explicit(&lk->lock, memory_order_release);
	}

	static inline void ruyi_lock_destroy(ruyi_lock_t* lk)
	{
		atomic_flag_clear_explicit(&lk->lock, memory_order_relaxed);
	}

#else

	#include <pthread.h>

	typedef struct {
		pthread_spinlock_t lock;
	} ruyi_lock_t;
	
	static inline void ruyi_lock_init(ruyi_lock_t *lk)
	{
		pthread_spin_init(&lk->lock, PTHREAD_PROCESS_PRIVATE);
	}

	static inline void ruyi_lock(ruyi_lock_t* lk)
	{
		pthread_spin_lock(&lk->lock);
	}

	static inline int ruyi_trylock(ruyi_lock_t* lk)
	{
		return pthread_spin_trylock(&lk->lock) == 0 ? RUYI_LOCK_OK : RUYI_LOCK_BUSY;
	}

	static inline void ruyi_unlock(ruyi_lock_t* lk)
	{
		pthread_spin_unlock(&lk->lock);
	}

	static inline void ruyi_lock_destroy(ruyi_lock_t* lk)
	{
		pthread_spin_destroy(&lk->lock);
	}
	
#endif

#endif
