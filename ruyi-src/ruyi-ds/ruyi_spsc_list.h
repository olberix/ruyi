#ifndef RUYI_SPSC_LIST_H
#define RUYI_SPSC_LIST_H

#include <stddef.h>

typedef struct ruyi_spsc_list_t ruyi_spsc_list_t;
typedef void (*ruyi_spsc_list_val_free_t)(void*);

ruyi_spsc_list_t* ruyi_spsc_list_create(size_t); /* mt-safe */
void ruyi_spsc_list_push(ruyi_spsc_list_t*, const void*); /* mt-unsafe */
void* ruyi_spsc_list_pop(ruyi_spsc_list_t*); /* mt-unsafe, users should free return val after used */
void ruyi_spsc_list_destroy(ruyi_spsc_list_t**, ruyi_spsc_list_val_free_t); /* mt-unsafe, ensure list is not accessed before this function called, or memory may be leaked */

#endif
