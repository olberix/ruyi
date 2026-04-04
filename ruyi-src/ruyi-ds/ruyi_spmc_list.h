#ifndef RUYI_SPMC_LIST_H
#define RUYI_SPMC_LIST_H

#include <stddef.h>

typedef struct ruyi_spmc_list_t ruyi_spmc_list_t;

ruyi_spmc_list_t* ruyi_spmc_list_create(size_t); /* mt-safe */
void ruyi_spmc_list_push(ruyi_spmc_list_t*, void*); /* mt-unsafe */
void* ruyi_spmc_list_pop(ruyi_spmc_list_t*); /* mt-safe, users should free return val after used */
void ruyi_spmc_list_destroy(ruyi_spmc_list_t**); /* mt-unsafe, ensure list is not accessed before this function called */

#endif
