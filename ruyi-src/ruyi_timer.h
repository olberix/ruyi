#ifndef RUYI_TIMER_H
#define RUYI_TIMER_H

#include <stdint.h>

typedef void (*ruyi_timer_callback_t)(void*);
typedef void (*ruyi_timer_userdata_free_t)(void*);

void ruyi_timer_init(); /* mt_unsafe */
void* ruyi_timer_event(); /* mt_unsafe */
void ruyi_timer_notify_stop(); /* mt_safe */

uint64_t ruyi_timer_add(uint64_t, ruyi_timer_callback_t, void*, ruyi_timer_userdata_free_t); /* mt_safe, return 0 if failed */
void ruyi_timer_cancel(uint64_t); /* mt_safe */

#endif
