#ifndef RUYI_NET_H
#define RUYI_NET_H

#include "ruyi_dns.h"

#include <stdint.h>
#include <netinet/in.h>
#include <assert.h>

typedef enum RUYI_NET_EVENT_T {
	RUYI_NET_EVENT_LISTEN = 0,
	RUYI_NET_EVENT_CONNECT_ACTIVE,
	RUYI_NET_EVENT_CONNECT_PASSIVE,
	RUYI_NET_EVENT_READ_CLOSE,
	RUYI_NET_EVENT_WRITE_CLOSE,
	RUYI_NET_EVENT_DNS_RESULT,

	RUYI_NET_ID_ERROR,
	RUYI_NET_EVENT_READ,
	RUYI_NET_EVENT_WRITE,
} RUYI_NET_EVENT_T;

typedef enum RUYI_NET_CLOSE_T {
	RUYI_NET_CLOSE_SERVER = 0,
	RUYI_NET_CLOSE_CLIENT,
	RUYI_NET_CLOSE_ERROR,
} RUYI_NET_CLOSE_T;

typedef struct ruyi_net_read_t {
	const char* rstr;
	const struct read_node_t* rn;
	uint32_t len;
	uint32_t id; /* from which listen fd */
} ruyi_net_read_t;

typedef void (*write_str_free_t)(void*);
typedef struct ruyi_net_write_t {
	const char* wstr;
	uint32_t len;
	write_str_free_t free_func;
} ruyi_net_write_t;

typedef struct ruyi_net_listen_t {
	const char* hostname;
	const char* service;
	const struct sockaddr_storage* ai;
} ruyi_net_listen_t;

typedef struct ruyi_net_conn_psv_t {
	const struct sockaddr_storage* ai;
} ruyi_net_conn_psv_t;

typedef struct ruyi_net_conn_act_t {
	const char* hostname;
	const char* service;
	const struct sockaddr_storage* ai;
} ruyi_net_conn_act_t;

typedef struct ruyi_net_close_t {
	RUYI_NET_CLOSE_T type;
	int32_t errcode;
} ruyi_net_close_t;

typedef struct ruyi_dns_result_t {
	ruyi_dns_t* dns;
} ruyi_dns_result_t;

typedef union ruyi_net_data_t {
	ruyi_net_listen_t listen;
	ruyi_net_conn_psv_t conn_psv;
	ruyi_net_conn_act_t conn_act;
	ruyi_net_read_t read;
	ruyi_net_write_t write;
	ruyi_net_close_t close;
	ruyi_dns_result_t dns_result;
} ruyi_net_data_t;

typedef struct ruyi_net_msg_t {
	_Alignas(32) RUYI_NET_EVENT_T ev;
	_Alignas(4) uint32_t id;
	_Alignas(8) ruyi_net_data_t data;
} ruyi_net_msg_t;
static_assert(sizeof(ruyi_net_msg_t) == 32, "ruyi_net_msg_t size error");
static_assert(_Alignof(ruyi_net_msg_t) == 32, "ruyi_net_msg_t align error");

void ruyi_net_init(); /* mt-unsafe */

void ruyi_net_listen(const char*, const char*, int32_t); /* mt-safe */
void ruyi_net_connect(const char*, const char*, int32_t); /* mt-safe */
void ruyi_net_write(uint32_t, char*, size_t, write_str_free_t); /* mt-safe */
void ruyi_net_close(uint32_t, int32_t); /* mt-safe */
void ruyi_net_dns_result(ruyi_dns_t*); /* mt-safe */

void* ruyi_net_event(); /* mt-unsafe */
void ruyi_net_notify_stop(); /* mt_safe */


#endif
