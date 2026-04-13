#ifndef RUYI_MUTEX_LOCK_H
#define RUYI_MUTEX_LOCK_H

#include <pthread.h>
#include <stdint.h>

#define RUYI_MUTEX_LOCK_OK 1
#define RUYI_MUTEX_LOCK_BUSY 2

typedef struct {
    pthread_mutex_t mutexlock;
    pthread_cond_t cond;
} ruyi_mutexlock_t;

static inline void ruyi_mutex_init(ruyi_mutexlock_t *lk)
{
	pthread_mutex_init(&lk->mutexlock, NULL);
	pthread_cond_init(&lk->cond, NULL);
}

static inline void ruyi_mutex_lock(ruyi_mutexlock_t* lk)
{
	pthread_mutex_lock(&lk->mutexlock);
}

static inline int32_t ruyi_mutex_trylock(ruyi_mutexlock_t* lk)
{
	return pthread_mutex_trylock(&lk->mutexlock) == 0 ? RUYI_MUTEX_LOCK_OK : RUYI_MUTEX_LOCK_BUSY;
}

static inline void ruyi_mutex_unlock(ruyi_mutexlock_t* lk)
{
	pthread_mutex_unlock(&lk->mutexlock);
}

static inline void ruyi_mutex_wait(ruyi_mutexlock_t* lk)
{
	pthread_cond_wait(&lk->cond, &lk->mutexlock);
}

static inline void ruyi_mutex_signal(ruyi_mutexlock_t* lk)
{
	pthread_cond_signal(&lk->cond);
}

static inline void ruyi_mutex_broadcast(ruyi_mutexlock_t* lk)
{
	pthread_cond_broadcast(&lk->cond);
}

static inline void ruyi_mutex_destroy(ruyi_mutexlock_t* lk)
{
	pthread_cond_destroy(&lk->cond);
    pthread_mutex_destroy(&lk->mutexlock);
}

#endif
