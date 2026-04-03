#ifndef RUYI_DEF_H
#define RUYI_DEF_H

#include "ruyi_malloc.h"

#include <stdint.h>

typedef enum {
	RUYI_ETYPE_TIMER_BEGIN = 0,
	RUYI_ETYPE_TIMER_ADD,
	RUYI_ETYPE_TIMER_CANCEL,
	RUYI_ETYPE_TIMER_END,

	RUYI_ETYPE_NET_BEGIN,
	RUYI_ETYPE_NET_CLOSE_ACTIVE,
	RUYI_ETYPE_NET_WRITE,
	RUYI_ETYPE_NET_CLOSE_PASSIVE,
	RUYI_ETYPE_NET_CONNECT,
	RUYI_ETYPE_NET_READ,
	RUYI_ETYPE_NET_END,
} RUYI_EVENTTYPE;

typedef struct {
	int64_t event_type;
	void* data;
} ruyi_event_entry_t;

/*
#define REET_MALLOC(node, dt) \
    (node) = RUYI_MEM_ALLOC(sizeof(node)); \
    (node)->data = RUYI_MEM_ALLOC(sizeof(dt));

#define REET_FREE(pnode, dt) \
    RUYI_MEM_FREE(&(*(pnode))->data); \
    RUYI_MEM_FREE(pnode);
*/

#if defined(__aarch64__) && defined(__APPLE__)
	#define CACHE_LINE_SIZE 128
#else
	#define CACHE_LINE_SIZE 64
#endif

#endif
