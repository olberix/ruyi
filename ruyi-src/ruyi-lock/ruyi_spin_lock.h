#ifndef RUYI_spinLOCK_H
#define RUYI_spinLOCK_H

#include <pthread.h>
#include <stdint.h>

#include "ruyi_macros.h"

#define RUYI_SPIN_LOCK_OK 1
#define RUYI_SPIN_LOCK_BUSY 2

#ifdef RUYI_SPINLOCK_FREE

	#include <stdatomic.h>

	#if defined(__x86_64__)
		#define RUYI_CPU_PAUSE() __asm__ volatile("pause" ::: "memory")
	#elif defined(__aarch64__)
		#define RUYI_CPU_PAUSE() __asm__ volatile("yield" ::: "memory")
	#else
		#define RUYI_CPU_PAUSE() atomic_thread_fence(memory_order_seq_cst)
	#endif

	typedef struct {
		_Alignas(CACHE_LINE_SIZE) atomic_flag spinlock;
		char padding[CACHE_LINE_SIZE - sizeof(_Alignas(CACHE_LINE_SIZE) atomic_flag)];
	} ruyi_spinlock_t;

	static inline void ruyi_spin_init(ruyi_spinlock_t* lk)
	{
		atomic_flag_clear_explicit(&lk->spinlock, memory_order_relaxed);
	}

	static inline void ruyi_spin_lock(ruyi_spinlock_t* lk)
	{
		while (atomic_flag_test_and_set_explicit(&lk->spinlock, memory_order_acquire)) {
			// RUYI_CPU_PAUSE();
		}
	}

	static inline int32_t ruyi_spin_trylock(ruyi_spinlock_t* lk)
	{
		if (!atomic_flag_test_and_set_explicit(&lk->spinlock, memory_order_acquire)) {
			return RUYI_SPIN_LOCK_OK;
		}
		return RUYI_SPIN_LOCK_BUSY;
	}

	static inline void ruyi_spin_unlock(ruyi_spinlock_t* lk)
	{
		atomic_flag_clear_explicit(&lk->spinlock, memory_order_release);
	}

	static inline void ruyi_spin_destroy(ruyi_spinlock_t* lk)
	{
		atomic_flag_clear_explicit(&lk->spinlock, memory_order_relaxed);
	}

#else

	typedef struct {
		pthread_spinlock_t spinlock;
	} ruyi_spinlock_t;
	
	static inline void ruyi_spin_init(ruyi_spinlock_t *lk)
	{
		pthread_spin_init(&lk->spinlock, PTHREAD_PROCESS_PRIVATE);
	}

	static inline void ruyi_spin_lock(ruyi_spinlock_t* lk)
	{
		pthread_spin_lock(&lk->spinlock);
	}

	static inline int32_t ruyi_spin_trylock(ruyi_spinlock_t* lk)
	{
		return pthread_spin_trylock(&lk->spinlock) == 0 ? RUYI_SPIN_LOCK_OK : RUYI_SPIN_LOCK_BUSY;
	}

	static inline void ruyi_spin_unlock(ruyi_spinlock_t* lk)
	{
		pthread_spin_unlock(&lk->spinlock);
	}

	static inline void ruyi_spin_destroy(ruyi_spinlock_t* lk)
	{
		pthread_spin_destroy(&lk->spinlock);
	}
	
#endif

#endif
