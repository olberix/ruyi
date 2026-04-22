#include "ruyi_net.h"
#include "ruyi_macros.h"
#include "ruyi_malloc.h"
#include "ruyi_poll.h"
#include "ruyi_log.h"
#include "ruyi-ds/ruyi_mpsc_list.h"
#include "ruyi-ds/ruyi_spmc_list.h"

#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#define RUYI_NET_PACK_MAX_SIZE (64 * 1024)
#define RUYI_NET_READ_RING_SIZE (1 << 3)
#define RUYI_NET_ID_SLOT_BITS (24)
#define RUYI_NET_ID_VERSION_BITS (8)
#define RUYI_NET_MAX_SOCKETS (1 << 16)

static_assert(RUYI_NET_PACK_MAX_SIZE >= 1, "RUYI_NET_PACK_MAX_SIZE error");
static_assert(RUYI_NET_READ_RING_SIZE >= 4 && (RUYI_NET_READ_RING_SIZE & (RUYI_NET_READ_RING_SIZE - 1)) == 0, "RUYI_NET_READ_RING_SIZE error");
static_assert(RUYI_NET_ID_SLOT_BITS + RUYI_NET_ID_VERSION_BITS == sizeof(uint32_t) * 8, "BITS error");
static_assert(RUYI_NET_MAX_SOCKETS >= 1 && RUYI_NET_MAX_SOCKETS <= ((uint32_t)1 << RUYI_NET_ID_SLOT_BITS) && (RUYI_NET_MAX_SOCKETS & (RUYI_NET_MAX_SOCKETS - 1)) == 0, "RUYI_NET_MAX_SOCKETS error");
#define RUYI_NET_ID_VERSION_MASK ((1 << RUYI_NET_ID_VERSION_BITS) - 1)

typedef struct write_node_t {
	uint32_t len;
	uint32_t offset;
	char* wstr;
	write_str_free_t free_func;
	struct write_node_t* next;
} write_node_t;

typedef struct read_node_t {
	_Alignas(RUYI_CACHELINE_SIZE) _Atomic uint32_t ref;
	char padding[RUYI_CACHELINE_SIZE - sizeof(_Atomic uint32_t)];
	
	uint32_t len;
	char* rstr;
} read_node_t;

typedef struct ruyi_conn_t {
	struct sockaddr_storage sas;
	int32_t protocol;
	int32_t socktype;
	uint32_t id;
	int32_t fd;
	const char* hostname;
	const char* service;

	struct addrinfo* ai;
	struct addrinfo* ai_cur;
	bool passive;

	int32_t errcode;
	bool readable;
	bool writable;
	bool is_pollout;
	bool is_pollin;

	uint32_t ring_head;
	uint32_t ring_tail;
	read_node_t read_ring[RUYI_NET_READ_RING_SIZE];
	
	write_node_t* write_list_head;
	write_node_t* write_list_tail;
} ruyi_conn_t;

typedef struct ruyi_net_t {
	_Alignas(RUYI_CACHELINE_SIZE) _Atomic bool running;
	char padding[RUYI_CACHELINE_SIZE - sizeof(_Atomic bool)];

	ruyi_mpsc_list_t* input_event_list;
	ruyi_spmc_list_t* output_event_list;

	uint32_t sp_top;
	uint32_t slot_pool[RUYI_NET_MAX_SOCKETS];

	uint32_t conns_gc_idx;
	ruyi_conn_t conns[RUYI_NET_MAX_SOCKETS];

	int32_t poll_fd;
} ruyi_net_t;

static ruyi_net_t s_net_info;

void ruyi_net_init()
{
	memset(&s_net_info, 0, sizeof(s_net_info));

	s_net_info.input_event_list = ruyi_mpsc_list_create(sizeof(ruyi_net_msg_t));
	s_net_info.output_event_list = ruyi_spmc_list_create(sizeof(ruyi_net_msg_t));

	for (uint32_t i = 0; i < RUYI_NET_MAX_SOCKETS; i++) {
		s_net_info.slot_pool[i] = i + 1;
	}
	s_net_info.sp_top = RUYI_NET_MAX_SOCKETS;

	s_net_info.poll_fd = ruyi_poll_create();
	RUYI_EXIT_IF_MSG(s_net_info.poll_fd < 0, "ruyi_net_init(): poll create failed: %s\n", strerror(errno));

	atomic_store_explicit(&s_net_info.running, true, memory_order_release);
}

static void _input_free_(void* pi)
{

}

static void _output_free_(void* po)
{

}

static inline uint32_t _id_slot_(uint32_t id)
{
	return (id >> RUYI_NET_ID_VERSION_BITS);
}

static inline uint32_t _id_version_(uint32_t id)
{
	return (id & RUYI_NET_ID_VERSION_MASK);
}

static inline ruyi_conn_t* _get_conn_(uint32_t id)
{
	return s_net_info.conns + _id_slot_(id) - 1;
}

static inline bool _is_cleared_(ruyi_conn_t* c)
{
	return _id_slot_(c->id) == 0;
}

static inline void _clear_conn_(ruyi_conn_t* c)
{
	RUYI_RETURN_IFUL(_is_cleared_(c));

	if (c->hostname != NULL) {
		RUYI_MEM_FREE(&c->hostname);
	}
	if (c->service != NULL) {
		RUYI_MEM_FREE(&c->service);
	}

	close(c->fd);
	while (c->write_list_head != NULL) {
		write_node_t* tmp = c->write_list_head;
		c->write_list_head = tmp->next;
		tmp->free_func(tmp->wstr);
		RUYI_MEM_FREE(&tmp);
	}
	if (c->is_pollout || c->is_pollin) {
		ruyi_poll_del(s_net_info.poll_fd, c->fd);
	}

	for (uint32_t j = 0; j < RUYI_NET_READ_RING_SIZE; j++) {
		read_node_t* rn = c->read_ring + j;
		while (atomic_load_explicit(&rn->ref, memory_order_relaxed) != 0) {
			static struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000000}; /* 100ms */
			nanosleep(&ts, NULL);
		}
		if (rn->rstr != NULL) {
			RUYI_MEM_FREE(&rn->rstr);
		}
	}

	uint32_t version = _id_version_(c->id);
	memset(c, 0, sizeof(*c));
	if (ruyi_likely(version < RUYI_NET_ID_VERSION_MASK)) {
		c->id = version + 1;
	}
}

static inline void _net_cleanup_()
{
	for (uint32_t idx = 0; idx < RUYI_NET_MAX_SOCKETS; idx++) {
		_clear_conn_(s_net_info.conns + idx);
	}

	ruyi_poll_close(s_net_info.poll_fd);

	struct timespec ts = {.tv_sec = 0, .tv_nsec = 500000000}; /* 500ms */
	nanosleep(&ts, NULL);
	ruyi_mpsc_list_destroy(&s_net_info.input_event_list, _input_free_);
	ruyi_spmc_list_destroy(&s_net_info.output_event_list, _output_free_);
}

static inline void _report_error_(ruyi_conn_t* c, int32_t errcode)
{
	c->errcode = errcode;

	ruyi_net_msg_t msg;
	msg.ev = RUYI_NET_EVENT_READ_CLOSE;
	msg.id = c->id;
	msg.data.close.type = RUYI_NET_CLOSE_ERROR;
	msg.data.close.errcode = c->errcode;
	ruyi_spmc_list_push(s_net_info.output_event_list, &msg);

	msg.ev = RUYI_NET_EVENT_WRITE_CLOSE;
	ruyi_spmc_list_push(s_net_info.output_event_list, &msg);
}

static inline void _conn_gc_()
{
	ruyi_conn_t* c = s_net_info.conns + s_net_info.conns_gc_idx;
	s_net_info.conns_gc_idx = (s_net_info.conns_gc_idx + 1) & RUYI_NET_MAX_SOCKETS;
	RUYI_RETURN_IF(_is_cleared_(c));
	
	if (c->errcode == 0) {
		RUYI_RETURN_IF(c->readable || c->writable || c->write_list_head != NULL);
	}
	for (uint32_t j = 0; j < RUYI_NET_READ_RING_SIZE; j++) {
		RUYI_RETURN_IF(atomic_load_explicit(&c->read_ring[j].ref, memory_order_relaxed) != 0);
	}
	
	uint32_t slot = _id_slot_(c->id);
	_clear_conn_(c);
	if (ruyi_likely(c->id < RUYI_NET_ID_VERSION_MASK)) {
		s_net_info.slot_pool[s_net_info.sp_top] = slot;
		s_net_info.sp_top++;
	}
}

static inline ruyi_conn_t* _spawn_conn_()
{
	RUYI_RETURN_VAL_IFUL(s_net_info.sp_top == 0, NULL);

	s_net_info.sp_top--;
	uint32_t slot = s_net_info.slot_pool[s_net_info.sp_top];
	uint32_t id = slot << RUYI_NET_ID_VERSION_BITS;
	ruyi_conn_t* c = _get_conn_(id);
	c->id = id | c->id;
	return c;
}

static inline void _polling_strategy_(ruyi_conn_t* c)
{
	if (ruyi_unlikely(c->errcode != 0)) {
		if (c->is_pollin == true || c->is_pollout == true) {
			ruyi_poll_del(s_net_info.poll_fd, c->fd);
			c->is_pollin = false;
			c->is_pollout = false;
		}
		return;
	}

	bool r = c->readable;
	bool w = c->write_list_head != NULL;
	if (!r && !w) {
		if (c->is_pollin || c->is_pollout) {
			if (ruyi_unlikely(ruyi_poll_del(s_net_info.poll_fd, c->fd) < 0)) {
				_report_error_(c, errno);
				RUYI_LOG_ERROR("delete id(%u) from polling error: %s", c->id, strerror(c->errcode));
			}
			else {
				c->is_pollin = false;
				c->is_pollout = false;
			}
		}
	}
	else {
		if (c->is_pollin != r || c->is_pollout != w) {
			if (ruyi_unlikely(ruyi_poll_ctl(s_net_info.poll_fd, c->fd, c, r, w) < 0)) {
				_report_error_(c, errno);
				RUYI_LOG_ERROR("control id(%u) from polling error: %s", c->id, strerror(c->errcode));
			}
			else {
				c->is_pollin = r;
				c->is_pollout = w;
			}
		}
	}
}

static inline void _process_event_read_close_(ruyi_conn_t* c, ruyi_net_msg_t* msg)
{
	(void)msg;
	RUYI_RETURN_IFUL(c->readable == false);

	shutdown(c->fd, SHUT_RD);
	c->readable = false;
	_polling_strategy_(c);

	ruyi_net_msg_t m;
	m.ev = RUYI_NET_EVENT_READ_CLOSE;
	m.id = c->id;
	m.data.close.type = RUYI_NET_CLOSE_SERVER;
	ruyi_spmc_list_push(s_net_info.output_event_list, &m);
}

static inline void _process_event_write_close_(ruyi_conn_t* c, ruyi_net_msg_t* msg)
{
	(void)msg;
	RUYI_RETURN_IFUL(c->writable == false);

	if (c->write_list_head == NULL) {
		shutdown(c->fd, SHUT_WR);
	}
	c->writable = false;
	_polling_strategy_(c);

	ruyi_net_msg_t m;
	m.ev = RUYI_NET_EVENT_WRITE_CLOSE;
	m.id = c->id;
	m.data.close.type = RUYI_NET_CLOSE_SERVER;
	ruyi_spmc_list_push(s_net_info.output_event_list, &m);
}

static inline int32_t _do_pending_events_()
{
	int32_t num = 0;
	ruyi_net_msg_t* msg = NULL;
	while ((msg = ruyi_mpsc_list_pop(s_net_info.input_event_list)) != NULL) {
		num++;
		ruyi_conn_t* c = _get_conn_(msg->id);
		if (ruyi_unlikely(c->id != msg->id)) {
			RUYI_LOG_ERROR("id on slot is %s, but received %s", c->id, msg->id);
			RUYI_MEM_FREE(&msg);
			continue;
		}
		if (ruyi_unlikely(c->errcode != 0)) {
			RUYI_LOG_ERROR("the socket for id(%u) has already encountered an error: %s", c->id, strerror(c->errcode));
			RUYI_MEM_FREE(&msg);
			continue;
		}
		switch (msg->ev) {
			case RUYI_NET_EVENT_DNS_RESULT:
				/* code */
				break;
			case RUYI_NET_EVENT_READ_CLOSE:
				_process_event_read_close_(c, msg);
				break;
			case RUYI_NET_EVENT_WRITE_CLOSE:
				_process_event_write_close_(c, msg);
				break;
			case RUYI_NET_EVENT_WRITE:

			default:
				break;
		}

		RUYI_MEM_FREE(&msg);
	}

	return num;
}

static inline int32_t _do_polling_events_()
{
	return 0;
}

void* ruyi_net_event()
{
	while (atomic_load_explicit(&s_net_info.running, memory_order_relaxed)) {
		_conn_gc_();

		int32_t num = 0;
		num += _do_pending_events_();
		num += _do_polling_events_();

		if (num <= 0) {
			static struct timespec ts = {.tv_sec = 0, .tv_nsec = 200000}; /* 200us */
			nanosleep(&ts, NULL);
		}
	}

	_net_cleanup_();

	return NULL;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline ruyi_dns_t* _pack_dns_(const char* hostname, const char* service, int32_t protocol, bool passive)
{
	char* h = NULL;
	if (hostname != NULL) {
		size_t sz = strlen(hostname) + 1;
		h = RUYI_MEM_ALLOC(sz);
		memcpy(h, hostname, sz);
	}
	size_t sz = strlen(service) + 1;
	char* s = RUYI_MEM_ALLOC(sz);
	memcpy(s, service, sz);

	ruyi_dns_t* dns = RUYI_MEM_ALLOC(sizeof(ruyi_dns_t));
	dns->hostname = h;
	dns->service = s;
	dns->ai = NULL;
	dns->protocol = protocol;
	dns->socktype = protocol == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM;
	dns->errcode = 0;
	dns->passive = passive;

	return dns;
}

void ruyi_net_listen(const char* hostname, const char* service, int32_t protocol)
{
	RUYI_RETURN_IF_MSG(service == NULL || !(protocol == IPPROTO_TCP || protocol == IPPROTO_UDP), "ruyi_net_listen(): params error\n");

	ruyi_dns_t* dns = _pack_dns_(hostname, service, protocol, true);

	ruyi_dns_request(dns);
}

void ruyi_net_connect(const char* hostname, const char* service, int32_t protocol)
{
	RUYI_RETURN_IF_MSG(service == NULL || !(protocol == IPPROTO_TCP || protocol == IPPROTO_UDP), "ruyi_net_connect(): params error\n");

	ruyi_dns_t* dns = _pack_dns_(hostname, service, protocol, false);

	ruyi_dns_request(dns);
}

void ruyi_net_dns_result(ruyi_dns_t* dns)
{
	RUYI_RETURN_IF_MSG(dns == NULL, "ruyi_net_dns_result(): params error\n");

	ruyi_net_msg_t msg;
	msg.ev = RUYI_NET_EVENT_DNS_RESULT;
	msg.data.dns_result.dns = dns;

	ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
}

void ruyi_net_write(uint32_t id, char* str, size_t len, write_str_free_t free_func)
{
	RUYI_RETURN_IF_MSG(str == NULL || free_func == NULL, "ruyi_net_write(): params error\n");

	ruyi_net_msg_t msg;
	msg.ev = RUYI_NET_EVENT_WRITE;
	msg.id = id;
	msg.data.write.wstr = str;
	msg.data.write.len = len;
	msg.data.write.free_func = free_func;

	ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
}

void ruyi_net_close(uint32_t id, int32_t how)
{
	RUYI_RETURN_IF_MSG(how == SHUT_RD || how == SHUT_WR || how == SHUT_RDWR, "ruyi_net_close(): params error\n");

	ruyi_net_msg_t msg;
	msg.id = id;
	msg.data.close.type = RUYI_NET_CLOSE_SERVER;
	if (how == SHUT_RD) {
		msg.ev = RUYI_NET_EVENT_READ_CLOSE;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
	else if (how == SHUT_WR) {
		msg.ev = RUYI_NET_EVENT_WRITE_CLOSE;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
	else {
		msg.ev = RUYI_NET_EVENT_READ_CLOSE;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
		msg.ev = RUYI_NET_EVENT_WRITE_CLOSE;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
}

void ruyi_net_notify_stop()
{
	atomic_store_explicit(&s_net_info.running, false, memory_order_relaxed);
}
