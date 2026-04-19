#ifndef RUYI_MACROS_H
#define RUYI_MACROS_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__aarch64__) && defined(__APPLE__)
	#define RUYI_CACHELINE_SIZE 128
#else
	#define RUYI_CACHELINE_SIZE 64
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define ruyi_likely(x) __builtin_expect(!!(x), 1)
    #define ruyi_unlikely(x) __builtin_expect(!!(x), 0)
#else
    #define ruyi_likely(x) (x)
    #define ruyi_unlikely(x) (x)
#endif

#define RUYI_MSG(fmt, ...) \
	do { fprintf(stderr, "[Ruyi Warn] " fmt, ##__VA_ARGS__); } while (0)

#define RUYI_MSG_IF(cond, fmt, ...) \
	do { if (ruyi_unlikely(cond)) RUYI_MSG(fmt, ##__VA_ARGS__); } while (0)

#define RUYI_RETURN_IF(cond) \
		do { if (cond) return; } while (0)

#define RUYI_RETURN_IFL(cond) \
	do { if (ruyi_likely(cond)) return; } while (0)

#define RUYI_RETURN_IFUL(cond) \
	do { if (ruyi_unlikely(cond)) return; } while (0)

#define RUYI_RETURN_IF_MSG(cond, fmt, ...) \
	do { \
		if (ruyi_unlikely(cond)) { \
			RUYI_MSG(fmt, ##__VA_ARGS__); \
			return; \
		} \
	} while (0)

#define RUYI_RETURN_VAL_IF(cond, val) \
	do { if (cond) return (val); } while (0)

#define RUYI_RETURN_VAL_IFL(cond, val) \
	do { if (ruyi_likely(cond)) return (val); } while (0)

#define RUYI_RETURN_VAL_IFUL(cond, val) \
	do { if (ruyi_unlikely(cond)) return (val); } while (0)

#define RUYI_RETURN_VAL_IF_MSG(cond, val, fmt, ...) \
	do { \
		if (ruyi_unlikely(cond)) { \
			RUYI_MSG(fmt, ##__VA_ARGS__); \
			return (val); \
		} \
	} while (0)

#define RUYI_EXIT_IF_MSG(cond, fmt, ...) \
	do {\
		if (ruyi_unlikely(cond)) { \
			fprintf(stderr, "[Ruyi Fatal] " fmt, ##__VA_ARGS__); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

#endif
