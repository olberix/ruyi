#ifndef RUYI_TIMER_H
#define RUYI_TIMER_H

#include <stdint.h>

void ruyi_timer_init();
void* ruyi_timer_event(void* args);
void ruyi_timer_cleanup();

void ruyi_timer_notify_stop(); /* mt_safe */

uint64_t ruyi_timer_add(); /* mt_safe */
void ruyi_timer_cancel(); /* mt_safe */

#endif
