#ifndef RUYI_POLL_H
#define RUYI_POLL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __linux__
	#include <sys/epoll.h>
	typedef struct epoll_event ruyi_poll_event_t;
#else
	#include <sys/event.h>
	typedef struct kevent ruyi_poll_event_t;
#endif

int32_t ruyi_poll_create();
int32_t ruyi_poll_close(int32_t);
int32_t ruyi_poll_ctl(int32_t, int32_t , void*, bool, bool, bool);

int32_t ruyi_poll_wait(int32_t, ruyi_poll_event_t*, int);
bool ruyi_poll_event_readable(const ruyi_poll_event_t*);
bool ruyi_poll_event_writable(const ruyi_poll_event_t*);
bool ruyi_poll_event_error(const ruyi_poll_event_t*);

#endif
