#ifndef RUYI_NET_H
#define RUYI_NET_H

#include "ruyi_dns.h"

#include <stdint.h>
#include <netinet/in.h>

typedef enum {
	RUYI_NET_EVENT_LISTEN = 0,
	RUYI_NET_EVENT_CONNECT_ACTIVE,
	RUYI_NET_EVENT_CONNECT_PASSIVE,
	RUYI_NET_EVENT_READ_CLOSED,
	RUYI_NET_EVENT_WRITE_CLOSED,
	RUYI_NET_EVENT_DNS_RESULT,

	RUYI_NET_EVENT_READ,
	RUYI_NET_EVENT_WRITE,
} ruyi_net_event_t;

typedef struct {
	const char* rstr;
	const struct read_node_t* rn;
	uint32_t len;
} ruyi_net_read_t;

typedef struct {
	const char* wstr;
	uint32_t len;
} ruyi_net_write_t;

typedef struct {
	const char* hostname;
	const char* service;
	const struct sockaddr_storage* ai;
} ruyi_net_listen_t;

typedef struct {
	const struct sockaddr_storage* ai;
} ruyi_net_conn_psv_t;

typedef struct {
	const char* hostname;
	const char* service;
	const struct sockaddr_storage* ai;
} ruyi_net_conn_act_t;

typedef struct {
	bool is_me;
} ruyi_net_close_t;

typedef struct {
	ruyi_dns_t* dns;
} ruyi_dns_result_t;

typedef union {
	ruyi_net_listen_t listen;
	ruyi_net_conn_psv_t conn_psv;
	ruyi_net_conn_act_t conn_act;
	ruyi_net_read_t read;
	ruyi_net_write_t write;
	ruyi_net_close_t close;
	ruyi_dns_result_t dns_result;
} ruyi_net_data_t;

typedef struct {
	ruyi_net_event_t ev;
	uint32_t id;
	ruyi_net_data_t data;
} ruyi_net_msg_t;


void ruyi_net_init(); /* mt-unsafe */

void ruyi_net_listen(const char*, const char*, int32_t); /* mt-safe */
void ruyi_net_connect(const char*, const char*, int32_t); /* mt-safe */
void ruyi_net_write(uint32_t, const char*, size_t); /* mt-safe */
void ruyi_net_close(uint32_t, int32_t); /* mt-safe */
void ruyi_net_dns_result(ruyi_dns_t*); /* mt-safe */

void* ruyi_net_event(); /* mt-unsafe */
void ruyi_net_notify_stop(); /* mt_safe */


#endif
