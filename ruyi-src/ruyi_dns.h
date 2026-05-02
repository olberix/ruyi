#ifndef RUYI_DNS_H
#define RUYI_DNS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdbool.h>

typedef struct {
	char* hostname;
	char* service;
	struct addrinfo* ai;
	
	int32_t protocol;
	int32_t socktype;
	int32_t errcode;
	bool passive;
} ruyi_dns_t;

ruyi_dns_t* ruyi_dns_new(const char*, const char*, int32_t, bool);
void ruyi_dns_destroy(ruyi_dns_t**);

void ruyi_dns_init(); /* mt_unsafe */
void* ruyi_dns_event(); /* mt_unsafe */
void ruyi_dns_notify_stop(); /* mt_safe */

void ruyi_dns_request(ruyi_dns_t*); /* mt_safe */

#endif
