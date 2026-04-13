#include "ruyi_socket.h"
#include "ruyi_macros.h"
#include "ruyi_malloc.h"

#include <string.h>

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

void ruyi_socket_listen(const char* hostname, const char* service, int32_t protocol)
{
	RUYI_RETURN_IF_MSG(service == NULL || !(protocol == IPPROTO_TCP || protocol == IPPROTO_UDP), "ruyi_socket_listen(): param error\n");

	ruyi_dns_t* dns = _pack_dns_(hostname, service, protocol, true);

	ruyi_dns_request(dns);
}

void ruyi_socket_connect(const char* hostname, const char* service, int32_t protocol)
{
	RUYI_RETURN_IF_MSG(service == NULL || !(protocol == IPPROTO_TCP || protocol == IPPROTO_UDP), "ruyi_socket_connect(): param error\n");

	ruyi_dns_t* dns = _pack_dns_(hostname, service, protocol, false);

	ruyi_dns_request(dns);
}

void ruyi_socket_dns_result(ruyi_dns_t* dns)
{
	
}
