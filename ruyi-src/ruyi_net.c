#include "ruyi_net.h"
#include "ruyi_macros.h"
#include "ruyi_malloc.h"
#include "ruyi-ds/ruyi_mpsc_list.h"
#include "ruyi-ds/ruyi_spmc_list.h"

#include <string.h>
#include <stdatomic.h>
#include <assert.h>

#define RUYI_NET_READ_RING_SIZE (1 << 3)
#define RUYI_NET_MAX_SOCKETS (1 << 16)

static_assert(RUYI_NET_READ_RING_SIZE >= 4 && (RUYI_NET_READ_RING_SIZE & (RUYI_NET_READ_RING_SIZE - 1)) == 0, "RUYI_NET_READ_RING_SIZE error");
static_assert(RUYI_NET_MAX_SOCKETS >= sizeof(uint64_t) && (RUYI_NET_MAX_SOCKETS & (RUYI_NET_MAX_SOCKETS - 1)) == 0, "RUYI_NET_MAX_SOCKETS error");

typedef struct {
	uint32_t len;
	uint32_t offset;
	char* wstr;
	struct write_node_t* next;
} write_node_t;

typedef struct {
	_Alignas(RUYI_CACHELINE_SIZE) _Atomic uint32_t ref;
	char padding[RUYI_CACHELINE_SIZE - sizeof(_Atomic uint32_t)];
	
	uint32_t len;
	char* rstr;
} read_node_t;

typedef struct {
	uint32_t id;
	int32_t fd;
	struct sockaddr_storage sas;
	int32_t protocol;
	int32_t socktype;
	char* hostname;
	char* service;

	struct addrinfo* ai;
	struct addrinfo* ai_cur;
	bool passive;

	bool readable;
	bool writable;

	uint32_t rd_h;
	uint32_t rd_t;
	read_node_t read_buffer[RUYI_NET_READ_RING_SIZE];
	
	write_node_t* write_buffer_h;
	write_node_t* write_buffer_t;
} ruyi_conn_t;

typedef struct {
	_Alignas(RUYI_CACHELINE_SIZE) _Atomic bool running;
	char padding[RUYI_CACHELINE_SIZE - sizeof(_Atomic bool)];

	ruyi_mpsc_list_t* input_event_list;
	ruyi_spmc_list_t* output_event_list;

	uint64_t id_sentinel;
	uint64_t id_maps[RUYI_NET_MAX_SOCKETS / sizeof(uint64_t)];
	ruyi_conn_t conns[RUYI_NET_MAX_SOCKETS];
} ruyi_net_t;

static ruyi_net_t s_net_info;

void ruyi_net_init()
{
	memset(&s_net_info, 0, sizeof(s_net_info));

	s_net_info.input_event_list = ruyi_mpsc_list_create(sizeof(ruyi_net_msg_t));
	s_net_info.output_event_list = ruyi_spmc_list_create(sizeof(ruyi_net_msg_t));

	atomic_store_explicit(&s_net_info.running, true, memory_order_release);
}

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
	RUYI_RETURN_IF_MSG(service == NULL || !(protocol == IPPROTO_TCP || protocol == IPPROTO_UDP), "ruyi_net_listen(): param error\n");

	ruyi_dns_t* dns = _pack_dns_(hostname, service, protocol, true);

	ruyi_dns_request(dns);
}

void ruyi_net_connect(const char* hostname, const char* service, int32_t protocol)
{
	RUYI_RETURN_IF_MSG(service == NULL || !(protocol == IPPROTO_TCP || protocol == IPPROTO_UDP), "ruyi_net_connect(): param error\n");

	ruyi_dns_t* dns = _pack_dns_(hostname, service, protocol, false);

	ruyi_dns_request(dns);
}

void ruyi_net_dns_result(ruyi_dns_t* dns)
{
	RUYI_RETURN_IF_MSG(dns == NULL, "ruyi_net_dns_result(): dns error\n");

	ruyi_net_msg_t msg;
	msg.ev = RUYI_NET_EVENT_DNS_RESULT;
	msg.data.dns_result.dns = dns;

	ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
}

void ruyi_net_write(uint32_t id, const char* str, size_t len)
{
	RUYI_RETURN_IF_MSG(str == NULL, "ruyi_net_write(): str error\n");

	ruyi_net_msg_t msg;
	msg.ev = RUYI_NET_EVENT_WRITE;
	msg.id = id;
	msg.data.write.wstr = str;
	msg.data.write.len = len;

	ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
}

void ruyi_net_close(uint32_t id, int32_t how)
{
	RUYI_RETURN_IF_MSG(how == SHUT_RD || how == SHUT_WR || how == SHUT_RDWR, "ruyi_net_close(): how error\n");

	ruyi_net_msg_t msg;
	msg.id = id;
	msg.data.close.is_me = true;
	if (how == SHUT_RD) {
		msg.ev = RUYI_NET_EVENT_READ_CLOSED;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
	else if (how == SHUT_WR) {
		msg.ev = RUYI_NET_EVENT_WRITE_CLOSED;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
	else {
		msg.ev = RUYI_NET_EVENT_READ_CLOSED;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
		msg.ev = RUYI_NET_EVENT_WRITE_CLOSED;
		ruyi_mpsc_list_push(s_net_info.input_event_list, &msg);
	}
}

void* ruyi_net_event()
{

	return NULL;
}


void ruyi_net_notify_stop()
{
	atomic_store_explicit(&s_net_info.running, false, memory_order_relaxed);
}
