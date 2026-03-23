#include "ruyi_log.h"

#include <stdatomic.h>
#include <stdint.h>

#if defined(__aarch64__) && defined(__APPLE__)
    #define CACHE_LINE_SIZE 128
#else
    #define CACHE_LINE_SIZE 64
#endif

#define RUYI_LOGSIZE ((uint32_t)1 << 16)

typedef struct {
    _Alignas(CACHE_LINE_SIZE) _Atomic uint32_t head;
    _Alignas(CACHE_LINE_SIZE) _Atomic uint32_t tail;

    char* buffer[RUYI_LOGSIZE];
} ruyi_log_list_t;

void ruyi_log_start()
{
    
}
