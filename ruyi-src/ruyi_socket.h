#ifndef RUYI_SOCKET_H
#define RUYI_SOCKET_H

#include "ruyi_dns.h"

#include <stdint.h>

void ruyi_socket_listen(const char*, const char*, int32_t);
void ruyi_socket_connect(const char*, const char*, int32_t);
void ruyi_socket_dns_result(ruyi_dns_t*);



#endif
