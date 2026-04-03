#include "ruyi_log.h"
#include "ruyi_malloc.h"
#include "ruyi_util.h"
#include "ruyi_check.h"
#include "ruyi_def.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdarg.h>

#define RUYI_LOGMSGFETCH (16)
#define RUYI_LOGMSGSIZE ((size_t)128)
#define RUYI_LOGBUFFSIZE ((uint32_t)1 << 16)
static_assert(RUYI_LOGBUFFSIZE > 1, "RUYI_LOGBUFFSIZE must be greater than 1");
static_assert((RUYI_LOGBUFFSIZE & (RUYI_LOGBUFFSIZE - 1)) == 0, "RUYI_LOGBUFFSIZE must be a power of 2");

typedef struct {
	RUYI_LOGLEVEL level;
	char* buffer;
} ruyi_log_entry_t;

typedef struct {
	_Alignas(CACHE_LINE_SIZE) _Atomic uint32_t head;
	_Alignas(CACHE_LINE_SIZE) _Atomic uint32_t tail;
	_Atomic bool running;

	int32_t fd_info;
	int32_t fd_err;

	uint32_t drop_num[RUYI_LOGLEVEL_MAX];
	uint32_t print_num[RUYI_LOGLEVEL_MAX];
	uint32_t input_num[RUYI_LOGLEVEL_MAX];
	ruyi_log_entry_t log_entry[RUYI_LOGBUFFSIZE];
} ruyi_log_info_t;

static ruyi_log_info_t s_log_info;

void ruyi_log_init()
{
	atomic_store_explicit(&s_log_info.head, 0, memory_order_relaxed);
	atomic_store_explicit(&s_log_info.tail, 0, memory_order_relaxed);
	
	char pathname[256] = "./.ruyi/";
	mkdir(pathname, 0744);
	int32_t len = strlen(pathname);
	ruyi_clock_date_format(pathname + len, sizeof(pathname) - len, NULL);
	len = strlen(pathname);
	strcat(pathname + len, ".info.log");
	s_log_info.fd_info = open(pathname, O_WRONLY | O_APPEND | O_CREAT, 0644);
	RUYI_EXIT_IF(s_log_info.fd_info < 0, "_log_init_(): open %s failed: %s\n", pathname, strerror(errno));
	strcpy(pathname + len, ".error.log");
	s_log_info.fd_err = open(pathname, O_WRONLY | O_APPEND | O_CREAT, 0644);
	RUYI_EXIT_IF(s_log_info.fd_err < 0, "_log_init_(): open %s failed: %s\n", pathname, strerror(errno));

	memset(s_log_info.drop_num, 0, sizeof(s_log_info.drop_num));
	memset(s_log_info.print_num, 0, sizeof(s_log_info.print_num));
	memset(s_log_info.input_num, 0, sizeof(s_log_info.input_num));
	for (uint32_t i = 0; i < RUYI_LOGBUFFSIZE; i++) {
		char* res = RUYI_MEM_ALLOC(RUYI_LOGMSGSIZE);
		RUYI_EXIT_IF(res == NULL, "_log_init_(): memory allocation failed\n");
		s_log_info.log_entry[i].buffer = res;
	}

	atomic_store_explicit(&s_log_info.running, true, memory_order_relaxed);
}

static inline int32_t _log_getmsg_(int32_t count, char buff[][RUYI_LOGMSGFETCH * (RUYI_LOGMSGSIZE + 1)])
{
	char datefmt[32];
	ruyi_clock_date_format(datefmt, sizeof(datefmt), NULL);

	int32_t cnt = -1;
	while (++cnt < count) {
		uint32_t t = atomic_load_explicit(&s_log_info.tail, memory_order_relaxed);
		uint32_t h = atomic_load_explicit(&s_log_info.head, memory_order_acquire);

		if (h == t) {
			break;
		}
		const ruyi_log_entry_t* pe = s_log_info.log_entry + t;
		char* wtb = buff[pe->level];
		const char* rdb = pe->buffer;
		memcpy(wtb + strlen(wtb), datefmt, strlen(datefmt));
		strcat(wtb + strlen(wtb), " ");
		memcpy(wtb + strlen(wtb), rdb, strlen(rdb));
		wtb[strlen(wtb)] = '\n';
		s_log_info.print_num[pe->level]++;

		atomic_store_explicit(&s_log_info.tail, (t + 1) & (RUYI_LOGBUFFSIZE - 1), memory_order_release);
	}

	return cnt;
}

static inline int32_t _log_writemsg_(int32_t count)
{
	static char tmp[RUYI_LOGLEVEL_MAX][RUYI_LOGMSGFETCH * (RUYI_LOGMSGSIZE + 1)];
	memset(tmp, 0, sizeof(tmp));

	int32_t cnt = _log_getmsg_(count, tmp);
	ssize_t len = strlen(tmp[RUYI_LOGLEVEL_INFO]);
	RUYI_MSG_IF(write(s_log_info.fd_info, tmp[RUYI_LOGLEVEL_INFO], len) != len, "_log_writemsg_(): write didn't execute as expected\n");
	len = strlen(tmp[RUYI_LOGLEVEL_ERROR]);
	RUYI_MSG_IF(write(s_log_info.fd_err, tmp[RUYI_LOGLEVEL_ERROR], len) != len, "_log_writemsg_(): write didn't execute as expected\n");

	return cnt;
}

static inline void _log_cleanup_()
{
	while(_log_writemsg_(RUYI_LOGMSGFETCH) >= RUYI_LOGMSGFETCH) {}

	char endstr[1024] = {'\0'};
	sprintf(endstr,
		"==========================================================input logs: %u\n"
		"==========================================================print logs: %u\n"
		"===========================================================drop logs: %u\n",
		s_log_info.input_num[RUYI_LOGLEVEL_INFO], s_log_info.print_num[RUYI_LOGLEVEL_INFO], s_log_info.drop_num[RUYI_LOGLEVEL_INFO]);
	write(s_log_info.fd_info, endstr, strlen(endstr));
	memset(endstr, 0, sizeof(endstr));
	sprintf(endstr,
		"==========================================================input logs: %u\n"
		"==========================================================print logs: %u\n"
		"===========================================================drop logs: %u\n",
		s_log_info.input_num[RUYI_LOGLEVEL_ERROR], s_log_info.print_num[RUYI_LOGLEVEL_ERROR], s_log_info.drop_num[RUYI_LOGLEVEL_ERROR]);
	write(s_log_info.fd_err, endstr, strlen(endstr));

	close(s_log_info.fd_err);
	close(s_log_info.fd_info);

	for (uint32_t i = 0; i < RUYI_LOGBUFFSIZE; i++) {
		RUYI_MEM_FREE(&s_log_info.log_entry[i].buffer);
	}
}

void* ruyi_log_event()
{
	while (atomic_load_explicit(&s_log_info.running, memory_order_relaxed)) {
		int32_t cnt = _log_writemsg_(RUYI_LOGMSGFETCH);
		if(cnt < RUYI_LOGMSGFETCH){
			static struct timespec ts = {.tv_sec = 0, .tv_nsec = 2000000}; /* 2ms */
			nanosleep(&ts, NULL);
		}
	}

	_log_cleanup_();

	return NULL;
}

void ruyi_log_notify_stop()
{
	atomic_store_explicit(&s_log_info.running, false, memory_order_relaxed);
}

void ruyi_log_input(RUYI_LOGLEVEL lv, const char *fmt, ...)
{
	RUYI_RETURN_IF_MSG(!atomic_load_explicit(&s_log_info.running, memory_order_relaxed), "ruyi_log_input(): log stopped\n");
	RUYI_RETURN_IF_MSG(lv < RUYI_LOGLEVEL_INFO || lv >= RUYI_LOGLEVEL_MAX || strlen(fmt) == 0, "ruyi_log_input(): params error\n");

	char msg[RUYI_LOGMSGSIZE];
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	RUYI_RETURN_IF_MSG(len <= 0, "ruyi_log_input(): vsnprintf error\n");

	uint32_t h = atomic_load_explicit(&s_log_info.head, memory_order_relaxed);
	uint32_t t = atomic_load_explicit(&s_log_info.tail, memory_order_acquire);

	s_log_info.input_num[lv]++;
	uint32_t next = (h + 1) & (RUYI_LOGBUFFSIZE - 1);
	if (next == t) {
		s_log_info.drop_num[lv]++;
		return;
	}

	char* bf = s_log_info.log_entry[h].buffer;
	memcpy(bf, msg, len + 1);
	s_log_info.log_entry[h].level = lv;

	atomic_store_explicit(&s_log_info.head, next, memory_order_release);
}
