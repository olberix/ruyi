#ifndef RUYI_LOG_H
#define RUYI_LOG_H

/* SPSC mode for network I/O only, mt_unsafe */

typedef enum {
    RUYI_LOGLEVEL_INFO = 0,
    RUYI_LOGLEVEL_ERROR,
    RUYI_LOGLEVEL_MAX
} RUYI_LOGLEVEL;

void* ruyi_log_event(void*);

void ruyi_log_notify_stop(); /* mt_safe */

void ruyi_log_input(RUYI_LOGLEVEL lv, const char* msg, ...);

#define RUYI_LOG_INFO(fmt, ...) ruyi_log_input(RUYI_LOGLEVEL_INFO, fmt, ##__VA_ARGS__)
#define RUYI_LOG_ERROR(fmt, ...) ruyi_log_input(RUYI_LOGLEVEL_ERROR, fmt, ##__VA_ARGS__)

#endif
