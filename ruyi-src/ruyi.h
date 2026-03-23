#ifndef RUYI_H
#define RUYI_H

#include "ruyi_malloc.h"
#include "ruyi_log.h"
#include "ruyi_timer.h"

static inline void ruyi_start()
{
    ruyi_mem_alloc_init(NULL);
    ruyi_log_start();
    ruyi_timer_start();
}

static inline void ruyi_stop()
{
    ruyi_timer_stop();
    ruyi_log_stop();
}

#endif
