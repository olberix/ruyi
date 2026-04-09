#ifndef RUYI_H
#define RUYI_H

#include "ruyi_malloc.h"
#include "ruyi_log.h"
#include "ruyi_timer.h"
#include "ruyi_macros.h"

#include <pthread.h>

typedef struct {
	pthread_t log_thread;
	pthread_t timer_thread;
} ruyi_workers_t;

static ruyi_workers_t s_ruyi_workers;

static inline void ruyi_start()
{
	ruyi_log_init();
	int ret = pthread_create(&s_ruyi_workers.log_thread, NULL, ruyi_log_event, NULL);
	RUYI_EXIT_IF_MSG(ret != 0, "ruyi_start(): log thread create failed: %s\n", strerror(ret));

	ruyi_timer_init();
	ret = pthread_create(&s_ruyi_workers.timer_thread, NULL, ruyi_timer_event, NULL);
	RUYI_EXIT_IF_MSG(ret != 0, "ruyi_start(): timer thread create failed: %s\n", strerror(ret));
}

static inline void ruyi_stop()
{
	ruyi_timer_notify_stop();
	pthread_join(s_ruyi_workers.timer_thread, NULL);

	ruyi_log_notify_stop();
	pthread_join(s_ruyi_workers.log_thread, NULL);
}

#endif
