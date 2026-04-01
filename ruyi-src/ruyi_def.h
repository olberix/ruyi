#ifndef RUYI_DEF_H
#define RUYI_DEF_H

#include "ruyi_malloc.h"

#include <stdint.h>

typedef struct {
    int64_t event_type;
    void* data;
} ruyi_event_entry_t;

#define REET_MALLOC(node, dt) \
    (node) = RUYI_MEM_ALLOC(sizeof(node)); \
    (node)->data = RUYI_MEM_ALLOC(sizeof(dt));

#define REET_FREE(pnode, dt) \
    RUYI_MEM_FREE(&(*(pnode))->data); \
    RUYI_MEM_FREE(pnode);

#endif
