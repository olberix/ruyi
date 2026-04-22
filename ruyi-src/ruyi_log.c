#include "ruyi_log.h"
#include "ruyi_malloc.h"
#include "ruyi_clock.h"
#include "ruyi_macros.h"
#include "ruyi-ds/ruyi_spsc_list.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>

#define RUYI_LOG_MSG_FETCH (8)
#define RUYI_LOG_MSG_SIZE ((size_t)128)

static_assert(RUYI_LOG_MSG_FETCH >= 1, "RUYI_LOG_MSG_FETCH error");
static_assert(RUYI_LOG_MSG_SIZE >= 1, "RUYI_LOG_MSG_SIZE error");

typedef struct {
	RUYI_LOGLEVEL level;
	char buffer[RUYI_LOG_MSG_SIZE];
} ruyi_log_msg_t;

typedef struct {
	_Alignas(RUYI_CACHELINE_SIZE) _Atomic bool running;
	char padding[RUYI_CACHELINE_SIZE - sizeof(_Atomic bool)];
	
	ruyi_spsc_list_t* msg_list;

	int32_t fd_info;
	int32_t fd_err;

	uint32_t log_count[RUYI_LOGLEVEL_MAX];
} ruyi_log_info_t;

static ruyi_log_info_t s_log_info;

void ruyi_log_init()
{
	memset(&s_log_info, 0, sizeof(s_log_info));

	char pathname[256] = "./.ruyi/";
	mkdir(pathname, 0744);
	int32_t len = strlen(pathname);
	ruyi_clock_date_format(pathname + len, sizeof(pathname) - len, NULL);
	len = strlen(pathname);
	strcat(pathname + len, ".info.log");
	s_log_info.fd_info = open(pathname, O_WRONLY | O_APPEND | O_CREAT, 0644);
	RUYI_EXIT_IF_MSG(s_log_info.fd_info < 0, "_log_init_(): open %s failed: %s\n", pathname, strerror(errno));
	strcpy(pathname + len, ".error.log");
	s_log_info.fd_err = open(pathname, O_WRONLY | O_APPEND | O_CREAT, 0644);
	RUYI_EXIT_IF_MSG(s_log_info.fd_err < 0, "_log_init_(): open %s failed: %s\n", pathname, strerror(errno));

	s_log_info.msg_list = ruyi_spsc_list_create(sizeof(ruyi_log_msg_t));
	memset(s_log_info.log_count, 0, sizeof(s_log_info.log_count));

	atomic_store_explicit(&s_log_info.running, true, memory_order_release);
}

static inline int32_t _log_getmsg_(int32_t count, char _buff[][RUYI_LOG_MSG_FETCH * (RUYI_LOG_MSG_SIZE + 64)])
{
	char datefmt[64];
	ruyi_clock_time_format(datefmt, sizeof(datefmt), NULL);

	int32_t cnt = -1;
	while (++cnt < count) {
		ruyi_log_msg_t* msg = ruyi_spsc_list_pop(s_log_info.msg_list);
		if (msg == NULL) {
			break;
		}

		char* write_buffer = _buff[msg->level];
		strcat(write_buffer + strlen(write_buffer), "[");
		memcpy(write_buffer + strlen(write_buffer), datefmt, strlen(datefmt));
		strcat(write_buffer + strlen(write_buffer), "] ");
		memcpy(write_buffer + strlen(write_buffer), msg->buffer, strlen(msg->buffer));
		write_buffer[strlen(write_buffer)] = '\n';

		RUYI_MEM_FREE(&msg);
	}

	return cnt;
}

static inline int32_t _log_writemsg_(int32_t count)
{
	static char tmp[RUYI_LOGLEVEL_MAX][RUYI_LOG_MSG_FETCH * (RUYI_LOG_MSG_SIZE + 64)];
	memset(tmp, 0, sizeof(tmp));

	int32_t cnt = _log_getmsg_(count, tmp);
	ssize_t len = strlen(tmp[RUYI_LOGLEVEL_INFO]);
	RUYI_MSG_IF(write(s_log_info.fd_info, tmp[RUYI_LOGLEVEL_INFO], len) != len, "_log_writemsg_(): write INFO didn't execute as expected\n");
	len = strlen(tmp[RUYI_LOGLEVEL_ERROR]);
	RUYI_MSG_IF(write(s_log_info.fd_err, tmp[RUYI_LOGLEVEL_ERROR], len) != len, "_log_writemsg_(): write ERROR didn't execute as expected\n");

	return cnt;
}

static inline void _log_cleanup_()
{
	while(_log_writemsg_(RUYI_LOG_MSG_FETCH) >= RUYI_LOG_MSG_FETCH) {}

	char endstr[1024] = {'\0'};
	sprintf(endstr,
		"==========================================================\n"
		"==========================================================log count: %u\n"
		"==========================================================\n",
		s_log_info.log_count[RUYI_LOGLEVEL_INFO]);
	write(s_log_info.fd_info, endstr, strlen(endstr));
	memset(endstr, 0, sizeof(endstr));
	sprintf(endstr,
		"==========================================================\n"
		"==========================================================log count: %u\n"
		"==========================================================\n",
		s_log_info.log_count[RUYI_LOGLEVEL_ERROR]);
	write(s_log_info.fd_err, endstr, strlen(endstr));

	close(s_log_info.fd_err);
	close(s_log_info.fd_info);

	struct timespec ts = {.tv_sec = 0, .tv_nsec = 500000000}; /* 500ms */
	nanosleep(&ts, NULL);
	ruyi_spsc_list_destroy(&s_log_info.msg_list, NULL);
}

void* ruyi_log_event()
{
	while (atomic_load_explicit(&s_log_info.running, memory_order_relaxed)) {
		int32_t cnt = _log_writemsg_(RUYI_LOG_MSG_FETCH);
		if(cnt < RUYI_LOG_MSG_FETCH){
			static struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000}; /* 1ms */
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

	ruyi_log_msg_t msg;
	msg.level = lv;
	va_list args;
	va_start(args, fmt);
	int32_t len = vsnprintf(msg.buffer, sizeof(msg.buffer), fmt, args);
	va_end(args);
	RUYI_RETURN_IF_MSG(len <= 0, "ruyi_log_input(): vsnprintf error\n");

	s_log_info.log_count[lv]++;
	ruyi_spsc_list_push(s_log_info.msg_list, &msg);
}
