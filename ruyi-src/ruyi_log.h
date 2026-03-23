#ifndef RUYI_LOG_H
#define RUYI_LOG_H

typedef enum {
    RUYI_LOGLEVEL_INFO = 0,
    RUYI_LOGLEVEL_ERROR,
} RUYI_LOGLEVEL;

void ruyi_log_start();
void ruyi_log_stop();

void ruyi_log_input();
void ruyi_log_output();

#endif
