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
#include <sys/uio.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <signal.h>

#define RUYI_NET_PACK_MAX_SIZE (64 * 1024)
#define RUYI_NET_READ_RING_SIZE (1 << 2)
#define RUYI_NET_READ_BUFF_SIZE (RUYI_NET_PACK_MAX_SIZE * 6)
#define RUYI_NET_READ_BUFF_INIT_CNT (1024)
#define RUYI_NET_ID_SLOT_BITS (24)
#define RUYI_NET_ID_VERSION_BITS (8)
#define RUYI_NET_MAX_SOCKETS (1 << 16)
#define RUYI_NET_BACKLOG (1024)

static_assert(RUYI_NET_PACK_MAX_SIZE >= 1, "RUYI_NET_PACK_MAX_SIZE error");
static_assert(RUYI_NET_READ_RING_SIZE >= 4 && (RUYI_NET_READ_RING_SIZE & (RUYI_NET_READ_RING_SIZE - 1)) == 0, "RUYI_NET_READ_RING_SIZE error");
static_assert(RUYI_NET_READ_BUFF_SIZE >= RUYI_NET_PACK_MAX_SIZE * 3, "RUYI_NET_READ_BUFF_SIZEv error");
static_assert(RUYI_NET_READ_BUFF_INIT_CNT >= 1, "RUYI_NET_READ_BUFF_INIT_CNT error");
static_assert(RUYI_NET_ID_SLOT_BITS + RUYI_NET_ID_VERSION_BITS == sizeof(uint32_t) * 8, "BITS error");
static_assert(RUYI_NET_MAX_SOCKETS >= 1 && RUYI_NET_MAX_SOCKETS <= ((uint32_t)1 << RUYI_NET_ID_SLOT_BITS) && RUYI_NET_MAX_SOCKETS >= sizeof(uint64_t) && (RUYI_NET_MAX_SOCKETS & (RUYI_NET_MAX_SOCKETS - 1)) == 0, "RUYI_NET_MAX_SOCKETS error");
static_assert(RUYI_NET_BACKLOG >= 1, "RUYI_NET_BACKLOG error");
#define RUYI_NET_ID_VERSION_MASK ((1 << RUYI_NET_ID_VERSION_BITS) - 1)
typedef struct ruyi_conn_t ruyi_conn_t;
static void _polling_strategy_(ruyi_conn_t*);
static void _send_close_(ruyi_conn_t*, int32_t, RUYI_NET_CLOSE_T);
#define REPORT_ERROR(c, code) \
	(c)->errcode = (code); \
	_send_close_(c, 0, 0); \
	_polling_strategy_(c);

typedef enum RUYI_NET_CONN_T {
	RUYI_NET_CONN_PASSIVE = 0,
	RUYI_NET_CONN_ACTIVE,
	RUYI_NET_CONN_LISTEN,
} RUYI_NET_CONN_T;

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
	
	char* rstr;
	uint32_t read_len;
	uint32_t parse_len;
} read_node_t;

typedef struct ruyi_conn_t {
	struct sockaddr_storage addr;
	int32_t protocol;
	int32_t socktype;
	uint32_t id;
	int32_t fd;
	const char* hostname;
	const char* service;

	struct addrinfo* ai;
	struct addrinfo* ai_cur;
	RUYI_NET_CONN_T conn_type;
	uint32_t conn_val;
	bool tcp_connecting;

	int32_t errcode;
	bool readable;
	bool writable;
	bool is_pollout;
	bool is_pollin;

	uint32_t ring_idx;
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

	uint32_t pwc_top;
	ruyi_conn_t* pending_write_conns[RUYI_NET_MAX_SOCKETS];
	uint64_t pwc_flags[RUYI_NET_MAX_SOCKETS / sizeof(uint64_t)];

	uint32_t rb_top;
	uint32_t rb_sz;
	char** rb_pool;
} ruyi_net_t;

static ruyi_net_t s_net_info;

static inline uint32_t _id_slot_(uint32_t id)
{
	return (id >> RUYI_NET_ID_VERSION_BITS);
}

static inline uint32_t _id_version_(uint32_t id)
{
	return (id & RUYI_NET_ID_VERSION_MASK);
}

static inline const char* _get_hostname_(ruyi_conn_t* c)
{
	if (c->hostname) {
		return c->hostname;
	}
	if (c->addr.ss_family == AF_INET) {
		return "0.0.0.0";
	}
	else {
		return "::";
	}
}

static inline const char* _get_conntype_(ruyi_conn_t* c)
{
	if (c->conn_type == RUYI_NET_CONN_LISTEN) {
		return "listen";
	}
	else if (c->conn_type == RUYI_NET_CONN_ACTIVE) {
		return "active";
	}
	else {
		return "passive";
	}
}

static inline bool _is_cleared_(ruyi_conn_t* c)
{
	return _id_slot_(c->id) == 0;
}

static inline char* _get_read_buff_()
{
	if (ruyi_unlikely(s_net_info.rb_top == 0)) {
		return RUYI_MEM_ALLOC(RUYI_NET_READ_BUFF_SIZE);
	}

	s_net_info.rb_top--;
	return s_net_info.rb_pool[s_net_info.rb_top];
}

static inline void _return_read_buff_(char* buff)
{
	if (ruyi_unlikely(s_net_info.rb_top >= s_net_info.rb_sz)) {
		s_net_info.rb_sz *= 2;
		s_net_info.rb_pool = RUYI_MEM_REALLOC(s_net_info.rb_pool, sizeof(char*) * s_net_info.rb_sz);
	}

	s_net_info.rb_pool[s_net_info.rb_top] = buff;
	s_net_info.rb_top++;
}

static inline void _clear_conn_(ruyi_conn_t* c)
{
	RUYI_RETURN_IFUL(_is_cleared_(c));

	RUYI_MEM_FREE(&c->hostname);
	RUYI_MEM_FREE(&c->service);

	if (c->fd >= 0) {
		close(c->fd);
	}
	while (c->write_list_head != NULL) {
		write_node_t* tmp = c->write_list_head;
		c->write_list_head = tmp->next;
		if (tmp->free_func) {
			tmp->free_func(tmp->wstr);
		}
		RUYI_MEM_FREE(&tmp);
	}
	if (c->is_pollout || c->is_pollin) {
		ruyi_poll_ctl(s_net_info.poll_fd, c->fd, NULL, false, false, true);
	}

	for (uint32_t j = 0; j < RUYI_NET_READ_RING_SIZE; j++) {
		read_node_t* rn = c->read_ring + j;
		while (atomic_load_explicit(&rn->ref, memory_order_relaxed) != 0) {
			static struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000000}; /* 100ms */
			nanosleep(&ts, NULL);
		}
		if (rn->rstr != NULL) {
			_return_read_buff_(rn->rstr);
		}
	}

	if (c->ai) {
		freeaddrinfo(c->ai);
	}

	uint32_t version = _id_version_(c->id);
	memset(c, 0, sizeof(*c));
	c->fd = -1;
	if (ruyi_likely(version < RUYI_NET_ID_VERSION_MASK)) {
		c->id = version + 1;
	}
}

void ruyi_net_init()
{
	memset(&s_net_info, 0, sizeof(s_net_info));

	s_net_info.input_event_list = ruyi_mpsc_list_create(sizeof(ruyi_net_msg_t));
	s_net_info.output_event_list = ruyi_spmc_list_create(sizeof(ruyi_net_msg_t));

	for (uint32_t idx = 0; idx < RUYI_NET_MAX_SOCKETS; idx++) {
		s_net_info.conns[idx].fd = -1;
		s_net_info.slot_pool[idx] = idx + 1;
	}
	s_net_info.sp_top = RUYI_NET_MAX_SOCKETS;

	s_net_info.poll_fd = ruyi_poll_create();
	RUYI_EXIT_IF_MSG(s_net_info.poll_fd < 0, "ruyi_net_init(): poll create failed: %s\n", strerror(errno));

	s_net_info.rb_sz = RUYI_NET_READ_BUFF_INIT_CNT;
	s_net_info.rb_pool = RUYI_MEM_ALLOC(sizeof(char*) * s_net_info.rb_sz);
	for (int i = 0; i < RUYI_NET_READ_BUFF_INIT_CNT; i++) {
		s_net_info.rb_pool[i] = RUYI_MEM_ALLOC(RUYI_NET_READ_BUFF_SIZE);
	}
	s_net_info.rb_top = RUYI_NET_READ_BUFF_INIT_CNT;

	signal(SIGPIPE, SIG_IGN);

	atomic_store_explicit(&s_net_info.running, true, memory_order_release);
}

static void _input_free_(void* pi)
{
	ruyi_net_msg_t* m = pi;
	if (m->ev == RUYI_NET_EVENT_DNS_RESULT) {
		RUYI_MEM_FREE(&m->data.dns_result.dns->hostname);
		RUYI_MEM_FREE(&m->data.dns_result.dns->service);
	}
	else if (m->ev == RUYI_NET_EVENT_WRITE) {
		m->data.write.free_func(m->data.write.wstr);
	}
}

static void _output_free_(void* po)
{
	ruyi_net_msg_t* m = po;
	if (m->ev == RUYI_NET_EVENT_READ) {
		read_node_t* rn = (read_node_t*)m->data.read.rn;
		atomic_fetch_sub_explicit(&rn->ref, 1, memory_order_relaxed);
	}
}

static inline ruyi_conn_t* _get_conn_(uint32_t id)
{
	return s_net_info.conns + _id_slot_(id) - 1;
}

static inline void _send_close_(ruyi_conn_t* c, int32_t what, RUYI_NET_CLOSE_T who)
{
	if (c->errcode != 0) {
		if (c->readable) {
			ruyi_net_msg_t msg;
			msg.ev = RUYI_NET_EVENT_READ_SHUTDOWN;
			msg.id = c->id;
			msg.data.close.type = RUYI_NET_CLOSE_ERROR;
			msg.data.close.errcode = c->errcode;
			ruyi_spmc_list_push(s_net_info.output_event_list, &msg);
			RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> read shutdown by error: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));
		}
		if (c->writable) {
			ruyi_net_msg_t msg;
			msg.ev = RUYI_NET_EVENT_WRITE_SHUTDOWN;
			msg.id = c->id;
			msg.data.close.type = RUYI_NET_CLOSE_ERROR;
			msg.data.close.errcode = c->errcode;
			ruyi_spmc_list_push(s_net_info.output_event_list, &msg);
			RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> write shutdown by error: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));
		}
		if (c->writable || c->readable) {
			ruyi_net_msg_t msg;
			msg.ev = RUYI_NET_EVENT_CLOSE;
			msg.id = c->id;
			msg.data.close.type = RUYI_NET_CLOSE_ERROR;
			msg.data.close.errcode = c->errcode;
			ruyi_spmc_list_push(s_net_info.output_event_list, &msg);
			RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> close by error: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));
		}
		return;
	}

	if (what == SHUT_RD) {
		ruyi_net_msg_t m;
		m.ev = RUYI_NET_EVENT_READ_SHUTDOWN;
		m.id = c->id;
		m.data.close.type = who;
		ruyi_spmc_list_push(s_net_info.output_event_list, &m);
		RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> read shutdown by %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), who == RUYI_NET_CLOSE_SERVER ? "server" : "client");

		if (c->writable == false) {
			m.ev = RUYI_NET_EVENT_CLOSE;
			ruyi_spmc_list_push(s_net_info.output_event_list, &m);
			RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> close by %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), who == RUYI_NET_CLOSE_SERVER ? "server" : "client");
		}
	}
	else {
		ruyi_net_msg_t m;
		m.ev = RUYI_NET_EVENT_WRITE_SHUTDOWN;
		m.id = c->id;
		m.data.close.type = who;
		ruyi_spmc_list_push(s_net_info.output_event_list, &m);
		RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> write shutdown by %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), who == RUYI_NET_CLOSE_SERVER ? "server" : "client");

		if (c->readable == false) {
			m.ev = RUYI_NET_EVENT_CLOSE;
			ruyi_spmc_list_push(s_net_info.output_event_list, &m);
			RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> close by %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), who == RUYI_NET_CLOSE_SERVER ? "server" : "client");
		}
	}
}

static inline void _net_cleanup_()
{
	ruyi_poll_close(s_net_info.poll_fd);

	struct timespec ts = {.tv_sec = 0, .tv_nsec = 500000000}; /* 500ms */
	nanosleep(&ts, NULL);
	ruyi_mpsc_list_destroy(&s_net_info.input_event_list, _input_free_);
	ruyi_spmc_list_destroy(&s_net_info.output_event_list, _output_free_);

	for (uint32_t i = 0; i < RUYI_NET_MAX_SOCKETS; i++) {
		_clear_conn_(s_net_info.conns + i);
	}

	for (uint32_t i = 0; i < s_net_info.rb_top; i++) {
		RUYI_MEM_FREE(&s_net_info.rb_pool[i]);
	}
	RUYI_MEM_FREE(&s_net_info.rb_pool);

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
	if (ruyi_unlikely(s_net_info.sp_top == 0)) {
		RUYI_LOG_ERROR("out of connection resources");
		return NULL;
	}

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
			ruyi_poll_ctl(s_net_info.poll_fd, c->fd, c, false, false, true);
			c->is_pollin = false;
			c->is_pollout = false;
		}
		return;
	}

	if (ruyi_unlikely(c->tcp_connecting == true)) {
		if (c->is_pollout == false) {
			if (ruyi_unlikely(ruyi_poll_ctl(s_net_info.poll_fd, c->fd, c, false, true, false) < 0)) {
				REPORT_ERROR(c, errno);
				RUYI_LOG_ERROR("<hostname:%s, service:%s, id:%u, type:%s> ruyi_poll_ctl1 failed: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));
			}
			else {
				c->is_pollout = true;
			}
		}
		return;
	}

	bool r = c->readable;
	bool w = c->write_list_head != NULL;
	if (c->is_pollin != r || c->is_pollout != w) {
		if (ruyi_unlikely(ruyi_poll_ctl(s_net_info.poll_fd, c->fd, c, r, w, (c->is_pollin || c->is_pollout)) < 0)) {
			REPORT_ERROR(c, errno);
			RUYI_LOG_ERROR("<hostname:%s, service:%s, id:%u, type:%s> ruyi_poll_ctl2 failed: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));
		}
		else {
			c->is_pollin = r;
			c->is_pollout = w;
		}
	}
}

static inline void _process_pending_read_shutdown_event_(ruyi_conn_t* c)
{
	RUYI_RETURN_IFUL(c->readable == false);
	
	c->readable = false;
	_send_close_(c, SHUT_RD, RUYI_NET_CLOSE_SERVER);

	shutdown(c->fd, SHUT_RD);
	_polling_strategy_(c);
}

static inline void _process_pending_write_shutdown_event_(ruyi_conn_t* c)
{
	RUYI_RETURN_IFUL(c->writable == false);
	
	c->writable = false;
	_send_close_(c, SHUT_WR, RUYI_NET_CLOSE_SERVER);
	
	if (c->write_list_head == NULL) {
		shutdown(c->fd, SHUT_WR);
	}
	_polling_strategy_(c);
}

static inline void _process_pending_write_event_(ruyi_conn_t* c, ruyi_net_msg_t* msg)
{
	RUYI_RETURN_IFUL(c->writable == false);

	write_node_t* node = RUYI_MEM_ALLOC(sizeof(write_node_t));
	node->wstr = msg->data.write.wstr;
	node->len = msg->data.write.len;
	node->offset = 0;
	node->free_func = msg->data.write.free_func;
	node->next = NULL;

	write_node_t* node_pre = RUYI_MEM_ALLOC(sizeof(write_node_t));
	uint32_t len = htonl(node->len);
	memcpy(&node_pre->wstr, &len, sizeof(uint32_t));
	node_pre->len = sizeof(uint32_t);
	node_pre->offset = 0;
	node_pre->free_func = NULL;
	node_pre->next = NULL;

	if (c->write_list_head == NULL) {
		c->write_list_head = node_pre;
		c->write_list_tail = node_pre;
	}
	else {
		c->write_list_tail->next = node_pre;
	}
	c->write_list_tail->next = node;

	if (c->is_pollout == false) {
		uint32_t s = _id_slot_(msg->id) - 1;
		uint32_t idx = s / 8;
		uint32_t mask = (1 << (s - idx * 8));
		if ((s_net_info.pwc_flags[idx] & mask) == 0) {
			s_net_info.pending_write_conns[s_net_info.pwc_top] = c;
			s_net_info.pwc_top++;
			s_net_info.pwc_flags[idx] |= mask;
		}
	}
}

static inline void _conn_write_(ruyi_conn_t* c)
{
	while (c->errcode == 0 && c->write_list_head != NULL) {
		#define CONN_WRITE_STEP (1024)
		struct iovec iov[CONN_WRITE_STEP];
		write_node_t* n = c->write_list_head;
		write_node_t* nn = n->next;
		ssize_t bytes = 0; uint32_t count = 0;
		for (uint32_t i = 0; i < CONN_WRITE_STEP && n != NULL; i++) {
			iov[i].iov_base = n->free_func ? (n->wstr + n->offset) : (((char*)(&n->wstr)) + n->offset);
			iov[i].iov_len = (n->len - n->offset);
			count++;
			bytes += (n->len - n->offset);
			n = nn;
			nn = nn->next;
		}

		ssize_t s = writev(c->fd, iov, count);
		if (s < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
				_polling_strategy_(c);
			}
			else if (errno == EPIPE) {
				c->writable = false;
				_send_close_(c, SHUT_WR, RUYI_NET_CLOSE_CLIENT);
				while (c->write_list_head != NULL) {
					write_node_t* tmp = c->write_list_head;
					c->write_list_head = tmp->next;
					if (tmp->free_func) {
						tmp->free_func(tmp->wstr);
					}
				}
			}
			else {
				REPORT_ERROR(c, errno);
				RUYI_LOG_ERROR("<hostname:%s, service:%s, id:%u, type:%s> writev failed: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));

			}
			break;
		}
		else {
			ssize_t b = s;
			while (b > 0 && b >= (c->write_list_head->len - c->write_list_head->offset)) {
				write_node_t* tmp = c->write_list_head;
				c->write_list_head = tmp->next;
				if (tmp->free_func) {
					tmp->free_func(tmp->wstr);
				}
				b -= (tmp->len - tmp->offset);

				RUYI_MEM_FREE(&tmp);
			}
			if (c->write_list_head == NULL) {
				c->write_list_tail = NULL;
			}
			else {
				c->write_list_head->offset = b;
			}

			if (s < bytes) {
				_polling_strategy_(c);
				break;
			}
		}
	}
}

static inline void _process_pending_final_write_action_()
{
	RUYI_RETURN_IF(s_net_info.pwc_top == 0);

	do {
		s_net_info.pwc_top--;
		ruyi_conn_t* c = s_net_info.pending_write_conns[s_net_info.pwc_top];

		_conn_write_(c);
	} while (s_net_info.pwc_top != 0);

	memset(s_net_info.pwc_flags, 0, sizeof(s_net_info.pwc_flags));
}

static inline int32_t _set_nonblocking_(int32_t fd)
{
	int32_t flags = fcntl(fd, F_GETFL, 0);
    if (ruyi_unlikely(flags < 0)) {
		return -1;
	}

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static inline void _do_tcp_listen_(ruyi_dns_t* dns)
{
	for (struct addrinfo* ai = dns->ai; ai != NULL; ai = ai->ai_next) {
		int32_t listen_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (ruyi_unlikely(listen_fd < 0)) {
			RUYI_LOG_ERROR("create listen socket failed: %s", strerror(errno));
			continue;
		}

		#ifdef SO_REUSEADDR
			int32_t reuse = 1;
			if (ruyi_unlikely(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)) {
				RUYI_LOG_ERROR("listen socket setsockopt SO_REUSEADDR failed: %s", strerror(errno));
				close(listen_fd);
				continue;
			}
		#endif

		#ifdef SO_KEEPALIVE
			int32_t keepalive = 1;
			if (ruyi_unlikely(setsockopt(listen_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0)) {
				RUYI_LOG_ERROR("listen socket setsockopt SO_KEEPALIVE failed: %s", strerror(errno));
				close(listen_fd);
				continue;
			}
		#endif

		if (ai->ai_family == AF_INET6) {
			#ifdef IPV6_V6ONLY
				int32_t v6only = 1;
				if (ruyi_unlikely(setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0)) {
					RUYI_LOG_ERROR("listen socket setsockopt IPV6_V6ONLY failed: %s", strerror(errno));
					close(listen_fd);
					continue;
				}
			#endif
		}

		if (ai->ai_protocol == IPPROTO_TCP) {
			#ifdef TCP_NODELAY
				int32_t nodelay = 1;
				if (ruyi_unlikely(setsockopt(listen_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)) {
					RUYI_LOG_ERROR("listen socket setsockopt TCP_NODELAY failed: %s", strerror(errno));
					close(listen_fd);
					continue;
				}
			#endif
			#ifdef TCP_DEFER_ACCEPT
				int32_t defer = 1;
				if (ruyi_unlikely(setsockopt(listen_fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &defer, sizeof(defer)) < 0)) {
					RUYI_LOG_ERROR("listen socket setsockopt TCP_DEFER_ACCEPT failed: %s", strerror(errno));
					close(listen_fd);
					continue;
				}
			#endif
		}
		
		if (ruyi_unlikely(_set_nonblocking_(listen_fd) < 0)) {
			RUYI_LOG_ERROR("listen socket _set_nonblocking_ failed: %s", strerror(errno));
			close(listen_fd);
			continue;
		}

		if (ruyi_unlikely(bind(listen_fd, ai->ai_addr, ai->ai_addrlen) < 0)) {
			RUYI_LOG_ERROR("bind failed: %s", strerror(errno));
			close(listen_fd);
			continue;
		}
		
		if (ruyi_unlikely(listen(listen_fd, RUYI_NET_BACKLOG) < 0)) {
			RUYI_LOG_ERROR("listen failed: %s", strerror(errno));
			close(listen_fd);
			continue;
		}

		ruyi_conn_t* c = _spawn_conn_();
		if (ruyi_likely(c != NULL)) {
			c->protocol = ai->ai_protocol;
			c->socktype = ai->ai_socktype;
			c->hostname = dns->hostname;
			c->service = dns->service;
			dns->hostname = NULL;
			dns->service = NULL;
			c->conn_type = RUYI_NET_CONN_LISTEN;
			c->fd = listen_fd;
			socklen_t addr_len = sizeof(c->addr);
			if (ruyi_unlikely(getsockname(listen_fd, (struct sockaddr*)&c->addr, &addr_len) < 0)) {
				REPORT_ERROR(c, errno);
				RUYI_LOG_ERROR("listen socket getsockname failed: %s", strerror(c->errcode));
			}
			c->readable = true;
			_polling_strategy_(c);

			ruyi_net_msg_t msg;
			msg.ev = RUYI_NET_EVENT_LISTEN;
			msg.id = c->id;
			msg.data.listen.hostname = c->hostname;
			msg.data.listen.service = c->service;
			msg.data.listen.addr = &c->addr;
			ruyi_spmc_list_push(s_net_info.output_event_list, &msg);
			RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> create listen socket successfully", _get_hostname_(c), c->service, c->id, _get_conntype_(c));
		}
		else {
			close(listen_fd);
			RUYI_LOG_ERROR("closing the listen socket causes no available connections");
		}
		break;
	}
}

static inline void _tcp_connect_success_(ruyi_conn_t* c)
{
	c->tcp_connecting = false;
	c->conn_type = RUYI_NET_CONN_ACTIVE;
	freeaddrinfo(c->ai);
	c->ai = NULL;
	c->ai_cur = NULL;
	c->readable = true;
	c->writable = true;
	_polling_strategy_(c);

	ruyi_net_msg_t msg;
	msg.ev = RUYI_NET_EVENT_CONNECT_ACTIVE;
	msg.id = c->id;
	msg.data.conn_act.hostname = c->hostname;
	msg.data.conn_act.service = c->service;
	msg.data.conn_act.addr = &c->addr;
	ruyi_spmc_list_push(s_net_info.output_event_list, &msg);

	RUYI_LOG_INFO("<hostname:%s, service:%s, id:%u, type:%s> connected", _get_hostname_(c), c->service, c->id, _get_conntype_(c));
}

static inline void _do_tcp_connect_(ruyi_conn_t* c)
{
	for (; c->ai_cur != NULL; c->ai_cur = c->ai_cur->ai_next) {
		if (c->fd >= 0) {
			close(c->fd);
			c->fd = -1;
		}
		c->fd = socket(c->ai_cur->ai_family, c->ai_cur->ai_socktype, c->ai_cur->ai_protocol);
		if (ruyi_unlikely(c->fd < 0)) {
			RUYI_LOG_ERROR("create connection socket failed: %s", strerror(errno));
			continue;
		}

		#ifdef SO_KEEPALIVE
			int32_t keepalive = 1;
			if (ruyi_unlikely(setsockopt(c->fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0)) {
				RUYI_LOG_ERROR("connection socket setsockopt SO_KEEPALIVE failed: %s", strerror(errno));
				continue;
			}
		#endif

		if (c->ai_cur->ai_protocol == IPPROTO_TCP) {
			#ifdef TCP_NODELAY
				int32_t nodelay = 1;
				if (ruyi_unlikely(setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)) {
					RUYI_LOG_ERROR("connection socket setsockopt TCP_NODELAY failed: %s", strerror(errno));
					continue;
				}
			#endif
		}

		if (ruyi_unlikely(_set_nonblocking_(c->fd) < 0)) {
			RUYI_LOG_ERROR("connection socket _set_nonblocking_ failed: %s", strerror(errno));
			continue;
		}

		int32_t status = connect(c->fd, c->ai_cur->ai_addr, c->ai_cur->ai_addrlen);
		if (status == 0) {
			_tcp_connect_success_(c);
			return;
		}
		else if (errno == EINPROGRESS) {
			_polling_strategy_(c);
			c->ai_cur = c->ai_cur->ai_next;
			return;
		}
	}
	
	if (c->ai_cur == NULL) {
		REPORT_ERROR(c, ETIMEDOUT);
		RUYI_LOG_ERROR("create connection socket to <%s, %s> failed: %s", _get_hostname_(c), c->service, strerror(c->errcode));
	}
}

static inline void _process_pending_dns_event_(ruyi_net_msg_t* msg)
{
	ruyi_dns_t* dns = msg->data.dns_result.dns;
	RUYI_RETURN_IF_MSG(dns == NULL, "dns is NULL\n");

	if (ruyi_unlikely(dns->ai == NULL)) {
		RUYI_LOG_ERROR("DNS resolution for <%s, %s> failed: %s", (dns->hostname ? dns->hostname : "NULL"), dns->service, gai_strerror(dns->errcode));
		return;
	}

	if (dns->passive) {
		_do_tcp_listen_(dns);
	}
	else {
		ruyi_conn_t* c = _spawn_conn_();
		if (c) {
			c->hostname = dns->hostname;
			c->service = dns->service;
			dns->hostname = NULL;
			dns->service = NULL;
			c->ai = dns->ai;
			c->ai_cur = dns->ai;
			dns->ai = NULL;
			c->tcp_connecting = true;
			_do_tcp_connect_(c);
		}
	}

	ruyi_dns_destroy(dns);
}

static inline int32_t _pending_events_dispatch_()
{
	int32_t num = 0;
	ruyi_net_msg_t* msg = NULL;
	while ((msg = ruyi_mpsc_list_pop(s_net_info.input_event_list)) != NULL) {
		num++;
		if (ruyi_unlikely(msg->ev == RUYI_NET_EVENT_DNS_RESULT)) {
			_process_pending_dns_event_(msg);
		}
		else {
			ruyi_conn_t* c = _get_conn_(msg->id);
			if (ruyi_unlikely(c->id != msg->id)) {
				RUYI_LOG_ERROR("id on slot is %s, but received %s", c->id, msg->id);
				goto PED_MSG_DATA_FREE;
			}
			if (ruyi_unlikely(c->errcode != 0)) {
				RUYI_LOG_ERROR("the socket for id(%u) has already encountered an error: %s", c->id, strerror(c->errcode));
				goto PED_MSG_DATA_FREE;
			}
			switch (msg->ev) {
				case RUYI_NET_EVENT_READ_SHUTDOWN:
					_process_pending_read_shutdown_event_(c);
					goto PED_MSG_DATA_FREE;
				case RUYI_NET_EVENT_WRITE_SHUTDOWN:
					_process_pending_write_shutdown_event_(c);
					goto PED_MSG_DATA_FREE;
				case RUYI_NET_EVENT_WRITE:
					_process_pending_write_event_(c, msg);
					goto PED_MSG_FREE;
				default:
					RUYI_LOG_ERROR("id(%u) event type error: %d", c->id, msg->ev);
					goto PED_MSG_DATA_FREE;
			}
		}
		
		PED_MSG_DATA_FREE:
		_input_free_(msg);
		PED_MSG_FREE:
		RUYI_MEM_FREE(&msg);
	}

	_process_pending_final_write_action_();

	return num;
}

static inline void _process_polling_error_event_(ruyi_conn_t* c)
{
	if (c->tcp_connecting == true) {
		_do_tcp_connect_(c);
		return;
	}

	int32_t so_error = 0;
    socklen_t len = sizeof(so_error);
    if (ruyi_unlikely(getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0)){
		REPORT_ERROR(c, errno);
		RUYI_LOG_ERROR("<hostname:%s, service:%s, id:%u, type:%s> getsockopt SO_ERROR failed: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));
	}
	else {
		REPORT_ERROR(c, so_error);
		RUYI_LOG_ERROR("<hostname:%s, service:%s, id:%u, type:%s> getsockopt SO_ERROR failed: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));
	}
}

static inline void _process_polling_write_event_(ruyi_conn_t* c)
{
	if (ruyi_unlikely(c->tcp_connecting == true)) {
		_tcp_connect_success_(c);
		return;
	}
	
	_conn_write_(c);
}

static inline void _process_polling_accept_event_(ruyi_conn_t* c)
{
	while (true) {
		struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);
		int32_t fd = accept(c->fd, (struct sockaddr*)&addr, &addr_len);
		if (fd < 0) {
			if (ruyi_unlikely(errno == EINTR)) {
				continue;
			}
			else if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			else {
				RUYI_LOG_ERROR("<hostname:%s, service:%s, id:%u, type:%s> accept new connection failed: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(errno));
			}
		}
		else {
			if (_set_nonblocking_(fd) < 0) {
				RUYI_LOG_ERROR("accepted connection _set_nonblocking_ failed: %s", strerror(errno));
				close(fd);
				continue;
			}
		}

		ruyi_conn_t* new_c = _spawn_conn_();
		new_c->conn_type = RUYI_NET_CONN_PASSIVE;
		new_c->conn_val = c->id;
		memcpy(&new_c->addr, &addr, sizeof(addr));
		new_c->protocol = c->protocol;
		new_c->socktype = c->socktype;
		char* service = RUYI_MEM_ALLOC(3);
		if (addr.ss_family == AF_INET) {
			char* hostname = RUYI_MEM_ALLOC(16);
			new_c->hostname = inet_ntop(AF_INET, &((const struct sockaddr_in*)&addr)->sin_addr, hostname, 16);
			snprintf(service, 3, "%u", ntohs(((const struct sockaddr_in*)&addr)->sin_port));
		}
		else {
			char* hostname = RUYI_MEM_ALLOC(46);
			new_c->hostname = inet_ntop(AF_INET6, &((const struct sockaddr_in6*)&addr)->sin6_addr, hostname, 46);
			snprintf(service, 3, "%u", ntohs(((const struct sockaddr_in6*)&addr)->sin6_port));
		}
		new_c->service = service;
		new_c->readable = true;
		new_c->writable = true;
		_polling_strategy_(new_c);

		ruyi_net_msg_t msg;
		msg.ev = RUYI_NET_EVENT_CONNECT_PASSIVE;
		msg.id = new_c->id;
		msg.data.conn_psv.listen_id = c->id;
		msg.data.conn_psv.addr = &new_c->addr;
		ruyi_spmc_list_push(s_net_info.output_event_list, &msg);
	}
}

static inline void _process_polling_read_event_(ruyi_conn_t* c)
{
	if (c->conn_type == RUYI_NET_CONN_LISTEN) {
		_process_polling_accept_event_(c);
		return;
	}

	for (uint32_t i = 0; i < RUYI_NET_READ_RING_SIZE; i++) {
		read_node_t* rn = c->read_ring + i;
		if (rn->read_len > 0 && rn->read_len == rn->parse_len && atomic_load_explicit(&rn->ref, memory_order_relaxed) == 0) {
			_return_read_buff_(rn->rstr);
			memset(rn, 0, sizeof(*rn));
		}
	}

	while (true) {
		read_node_t* rn = c->read_ring + c->ring_idx;
		if (rn->parse_len != 0 && rn->parse_len != rn->read_len && rn->parse_len + sizeof(uint32_t) > rn->read_len) {
			uint32_t ni = ((c->ring_idx + 1) & (RUYI_NET_READ_RING_SIZE - 1));
			read_node_t* rnn = c->read_ring + ni;
			if (rnn->read_len == 0) {
				rnn->rstr = _get_read_buff_();
				rnn->read_len = rn->read_len - rn->parse_len;
				memcpy(rnn->rstr, rn->rstr + rn->parse_len, rnn->read_len);
				rn->read_len -= rnn->read_len;
				c->ring_idx = ni;
				continue;
			}
			break;
		}

		size_t s = 0;
		if (rn->read_len <= sizeof(uint32_t)) {
			s = RUYI_NET_READ_BUFF_SIZE - RUYI_NET_PACK_MAX_SIZE - rn->read_len;
		}
		else if (rn->read_len != rn->parse_len) {
			s = ntohl(*((uint32_t*)(rn->rstr + rn->parse_len))) - (rn->read_len - (rn->parse_len + 4));
		}

		if (s > 0) {
			if (rn->rstr == NULL) {
				rn->rstr = _get_read_buff_();
			}

			ssize_t n;
			READ_AGAIN:
			n = read(c->fd, rn->rstr + rn->read_len, s);
			if (n < 0) {
				if (ruyi_unlikely(errno == EINTR)) {
					goto READ_AGAIN;
				}
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					break;
				}
				REPORT_ERROR(c, errno);
				RUYI_LOG_ERROR("<hostname:%s, service:%s, id:%u, type:%s> read failed: %s", _get_hostname_(c), c->service, c->id, _get_conntype_(c), strerror(c->errcode));
				return;
			}
			else if (n == 0) {
				c->readable = false;
				_send_close_(c, SHUT_RD, RUYI_NET_CLOSE_CLIENT);
				
				shutdown(c->fd, SHUT_RD);
				_polling_strategy_(c);
			}
			else {
				rn->read_len += n;
				while (rn->parse_len + sizeof(uint32_t) <= rn->read_len) {
					uint32_t len = ntohl(*((uint32_t*)(rn->rstr + rn->parse_len)));
					if (ruyi_unlikely(len > RUYI_NET_PACK_MAX_SIZE)) {
						REPORT_ERROR(c, EINVAL);
						RUYI_LOG_ERROR("<hostname:%s, service:%s, id:%u, type:%s> package size error", _get_hostname_(c), c->service, c->id, _get_conntype_(c));
						return;
					}
					if (rn->read_len < rn->parse_len + sizeof(uint32_t) + len) {
						break;
					}

					atomic_fetch_add_explicit(&rn->ref, 1, memory_order_acquire);
					ruyi_net_msg_t m;
					m.ev = RUYI_NET_EVENT_READ;
					m.id = c->id;
					m.data.read.rstr = rn->rstr + rn->parse_len + sizeof(uint32_t);
					m.data.read.len = len;
					m.data.read.rn = rn;
					ruyi_spmc_list_push(s_net_info.output_event_list, &m);

					rn->parse_len += sizeof(uint32_t) + len;
				}
				if (n < (ssize_t)s) {
					break;
				}
			}
		}
		else {
			uint32_t ni = ((c->ring_idx + 1) & (RUYI_NET_READ_RING_SIZE - 1));
			read_node_t* rnn = c->read_ring + ni;
			if (rnn->read_len == 0) {
				c->ring_idx = ni;
				continue;
			}
			break;
		}
	}
}

static inline int32_t _polling_events_dispatch_()
{
	#define POLLING_EVENT_STEP (1024)
	ruyi_poll_event_t events[POLLING_EVENT_STEP];
	int32_t num = ruyi_poll_wait(s_net_info.poll_fd, events, POLLING_EVENT_STEP);
	if (ruyi_unlikely(num < 0)) {
		RUYI_LOG_ERROR("ruyi_poll_wait failed: %s", strerror(errno));
		ruyi_log_notify_stop();
		static struct timespec ts = {.tv_sec = 2, .tv_nsec = 0}; /* 2s */
		nanosleep(&ts, NULL);
		exit(EXIT_FAILURE);
	}

	for (int32_t idx = 0; idx < num; idx++) {
		ruyi_poll_event_t* event = events + idx;
		ruyi_conn_t* c = event->data.ptr;
		if (ruyi_unlikely(c->errcode != 0)) {
			continue;
		}
		if (ruyi_unlikely(ruyi_poll_event_error(event) == true)) {
			_process_polling_error_event_(c);
			continue;
		}

		if(ruyi_poll_event_readable(event) == true) {
			_process_polling_read_event_(c);
		}
		if(ruyi_poll_event_writable(event) == true) {
			_process_polling_write_event_(c);
		}
	}

	return num;
}

void* ruyi_net_event()
{
	while (atomic_load_explicit(&s_net_info.running, memory_order_relaxed)) {
		_conn_gc_();

		int32_t num = 0;
		num += _pending_events_dispatch_();
		num += _polling_events_dispatch_();

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

void ruyi_net_listen(const char* hostname, const char* service, int32_t protocol)
{
	RUYI_RETURN_IF_MSG(service == NULL || !(protocol == IPPROTO_TCP || protocol == IPPROTO_UDP), "ruyi_net_listen(): params error\n");

	ruyi_dns_t* dns = ruyi_dns_new(hostname, service, protocol, true);

	ruyi_dns_request(dns);
}

void ruyi_net_connect(const char* hostname, const char* service, int32_t protocol)
{
	RUYI_RETURN_IF_MSG(service == NULL || !(protocol == IPPROTO_TCP || protocol == IPPROTO_UDP), "ruyi_net_connect(): params error\n");

	ruyi_dns_t* dns = ruyi_dns_new(hostname, service, protocol, false);

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

ruyi_net_msg_t* ruyi_net_get_msg()
{
	return ruyi_spmc_list_pop(s_net_info.output_event_list);
}

void ruyi_net_destroy_msg(ruyi_net_msg_t** msg)
{
	_output_free_(*msg);
	*msg = NULL;
}

void ruyi_net_send(uint32_t id, char* str, size_t len, write_str_free_t free_func)
{
	RUYI_RETURN_IF_MSG(str == NULL || free_func == NULL || len > RUYI_NET_PACK_MAX_SIZE, "ruyi_net_send(): params error\n");

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
		msg.ev = RUYI_NET_EVENT_READ_SHUTDOWN;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
	else if (how == SHUT_WR) {
		msg.ev = RUYI_NET_EVENT_WRITE_SHUTDOWN;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
	else {
		msg.ev = RUYI_NET_EVENT_READ_SHUTDOWN;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
		msg.ev = RUYI_NET_EVENT_WRITE_SHUTDOWN;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
}

void ruyi_net_notify_stop()
{
	atomic_store_explicit(&s_net_info.running, false, memory_order_relaxed);
}
