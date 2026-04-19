#ifndef RUYI_MPSC_LIST_H
#define RUYI_MPSC_LIST_H

#include <stddef.h>

typedef struct ruyi_mpsc_list_t ruyi_mpsc_list_t;
typedef void (*ruyi_mpsc_list_val_free_t)(void*);

ruyi_mpsc_list_t* ruyi_mpsc_list_create(size_t); /* mt-safe */
void ruyi_mpsc_list_push(ruyi_mpsc_list_t*, const void*); /* mt-safe */
void* ruyi_mpsc_list_pop(ruyi_mpsc_list_t*); /* mt-unsafe, users should free return val after used */
void ruyi_mpsc_list_destroy(ruyi_mpsc_list_t**, ruyi_mpsc_list_val_free_t); /* mt-unsafe, ensure list is not accessed before this function called */

#endif
