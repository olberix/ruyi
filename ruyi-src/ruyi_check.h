#ifndef RUYI_CHECK_H
#define RUYI_CHECK_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RUYI_MSG(fmt, ...) \
    do { fprintf(stderr, "[Ruyi Warn] " fmt, ##__VA_ARGS__); } while (0)

#define RUYI_MSG_IF(cond, fmt, ...) \
    do { if (cond) RUYI_MSG(fmt, ##__VA_ARGS__); } while (0)

#define RUYI_RETURN_IF(cond) \
    do { if (cond) return; } while (0)

#define RUYI_RETURN_IF_MSG(cond, fmt, ...) \
    do { \
        if (cond) { \
            RUYI_MSG(fmt, ##__VA_ARGS__); \
            return; \
        } \
    } while (0)

#define RUYI_RETURN_VAL_IF(cond, val) \
    do { if (cond) return (val); } while (0)

#define RUYI_RETURN_VAL_IF_MSG(cond, val, fmt, ...) \
    do { \
        if (cond) { \
            RUYI_MSG(fmt, ##__VA_ARGS__); \
            return (val); \
        } \
    } while (0)

#define RUYI_EXIT_IF(cond, fmt, ...) \
    do {\
        if (cond) { \
            fprintf(stderr, "[Ruyi Fatal] " fmt, ##__VA_ARGS__); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#endif
