#ifndef RUYI_UTIL_H
#define RUYI_UTIL_H

#ifndef _POSIX_C_SOURCE
	#define _POSIX_C_SOURCE 200809l
#endif

#include <time.h>
#include <stdint.h>
#include <stdio.h>

static inline struct timespec ruyi_clock_time_ts()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	
	return ts;
}

static inline uint64_t ruyi_clock_time_ms()
{
	struct timespec ts = ruyi_clock_time_ts();

	return ((uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec) / 1000000;
}

static inline uint64_t ruyi_clock_time_ns()
{
	struct timespec ts = ruyi_clock_time_ts();

	return ((uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec);
}

/* Output format: "YYYY-MM-DD HH:MM:SS.mmm" */
static inline void ruyi_clock_time_format(char* buf, size_t len, const struct timespec* pts)
{
	struct timespec ts = pts ? (*pts) : ruyi_clock_time_ts();
	struct tm _tm;
	localtime_r(&(ts.tv_sec), &_tm);
	
	snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d.%03ld", _tm.tm_year + 1900, _tm.tm_mon + 1, _tm.tm_mday,\
		_tm.tm_hour, _tm.tm_min, _tm.tm_sec, ts.tv_nsec / 1000000l);
}

/* Output format: "YYYY-MM-DD" */
static inline void ruyi_clock_date_format(char* buf, size_t len, const struct timespec* pts)
{
	struct timespec ts = pts ? (*pts) : ruyi_clock_time_ts();
	struct tm _tm;
	localtime_r(&(ts.tv_sec), &_tm);
	
	snprintf(buf, len, "%04d-%02d-%02d", _tm.tm_year + 1900, _tm.tm_mon + 1, _tm.tm_mday);
}

#endif
