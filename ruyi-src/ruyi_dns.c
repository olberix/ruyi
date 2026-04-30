#include "ruyi_dns.h"
#include "ruyi_net.h"
#include "ruyi_macros.h"
#include "ruyi_malloc.h"
#include "ruyi-ds/ruyi_spsc_list.h"
#include "ruyi-lock/ruyi_mutex_lock.h"

#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>

#define RUYI_DNS_THREAD_COUNT (5)

static_assert(RUYI_DNS_THREAD_COUNT >= 1, "RUYI_DNS_THREAD_COUNT error");

typedef struct {
	_Alignas(RUYI_CACHELINE_SIZE) _Atomic bool running;
	char padding[RUYI_CACHELINE_SIZE - sizeof(_Atomic bool)];
	
	ruyi_mutexlock_t mlock;
	pthread_t* threads;
	ruyi_spsc_list_t* dns_list;
} ruyi_dns_info_t;

static ruyi_dns_info_t s_dns_info;

void* _ruyi_dns_work_()
{
	while (atomic_load_explicit(&s_dns_info.running, memory_order_seq_cst)) {
		ruyi_dns_t** dns;
		ruyi_mutex_lock(&s_dns_info.mlock);
		if ((dns = ruyi_spsc_list_pop(s_dns_info.dns_list)) == NULL) {
			ruyi_mutex_wait(&s_dns_info.mlock);
			ruyi_mutex_unlock(&s_dns_info.mlock);
			continue;
		}
		ruyi_mutex_unlock(&s_dns_info.mlock);

		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		if((*dns)->passive == true) {
			hints.ai_flags |= AI_PASSIVE;
		}
		else {
			hints.ai_flags |= AI_ADDRCONFIG;
		}
		hints.ai_family = AF_UNSPEC;
		hints.ai_protocol = (*dns)->protocol;
		hints.ai_socktype = (*dns)->socktype;

		(*dns)->errcode = getaddrinfo((*dns)->hostname, (*dns)->service, &hints, &(*dns)->ai);
		ruyi_net_dns_result(*dns);
		RUYI_MEM_FREE(&dns);
	}

	return NULL;
}

void ruyi_dns_init()
{
	memset(&s_dns_info, 0, sizeof(s_dns_info));

	ruyi_mutex_init(&s_dns_info.mlock);
	s_dns_info.threads = RUYI_MEM_ALLOC(sizeof(pthread_t) * RUYI_DNS_THREAD_COUNT);
	s_dns_info.dns_list = ruyi_spsc_list_create(sizeof(ruyi_dns_t*));
	atomic_store_explicit(&s_dns_info.running, true, memory_order_release);

	for(int32_t i = 0; i < RUYI_DNS_THREAD_COUNT; i++) {
		int32_t ret = pthread_create(s_dns_info.threads + i, NULL, _ruyi_dns_work_, NULL);
		RUYI_EXIT_IF_MSG(ret != 0, "ruyi_dns_init(): dns work thread create failed: %s\n", strerror(ret));
	}
}

static void _dns_free_(void* pd)
{
	ruyi_dns_t* dns = *((ruyi_dns_t**)pd);
	RUYI_MEM_FREE(&dns);
}

static inline void _dns_cleanup_()
{
	for(int32_t i = 0; i < RUYI_DNS_THREAD_COUNT; i++) {
		pthread_join(s_dns_info.threads[i], NULL);
	}
	RUYI_MEM_FREE(&s_dns_info.threads);

	ruyi_mutex_destroy(&s_dns_info.mlock);
	
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 500000000}; /* 500ms */
	nanosleep(&ts, NULL);
	ruyi_spsc_list_destroy(&s_dns_info.dns_list, _dns_free_);
}

void* ruyi_dns_event()
{
	_dns_cleanup_();

	return NULL;
}

void ruyi_dns_notify_stop()
{
	atomic_store_explicit(&s_dns_info.running, false, memory_order_seq_cst);
	ruyi_mutex_broadcast(&s_dns_info.mlock);
}

void ruyi_dns_request(ruyi_dns_t* dns)
{
	RUYI_RETURN_IFUL(atomic_load_explicit(&s_dns_info.running, memory_order_relaxed) == false);

	ruyi_mutex_lock(&s_dns_info.mlock);
	ruyi_spsc_list_push(s_dns_info.dns_list, &dns);
	ruyi_mutex_signal(&s_dns_info.mlock);
	ruyi_mutex_unlock(&s_dns_info.mlock);
}

ruyi_dns_t* ruyi_dns_new(const char* hostname, const char* service, int32_t protocol, bool passive)
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

void ruyi_dns_destroy(ruyi_dns_t* dns)
{
	if (dns->hostname) {
		RUYI_MEM_FREE(&dns->hostname);
	}
	if (dns->service) {
		RUYI_MEM_FREE(&dns->service);
	}
	if (dns->ai) {
		freeaddrinfo(dns->ai);
	}
	RUYI_MEM_FREE(&dns);
}
