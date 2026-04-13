#include "ruyi_dns.h"
#include "ruyi_socket.h"
#include "ruyi_macros.h"
#include "ruyi_malloc.h"
#include "ruyi-ds/ruyi_spsc_list.h"
#include "ruyi-lock/ruyi_mutex_lock.h"

#include <pthread.h>
#include <stdatomic.h>

#define DNS_THREAD_COUNT 5

typedef struct {
	_Atomic bool running;
	char padding[CACHE_LINE_SIZE - sizeof(_Atomic bool)];
	
	ruyi_mutexlock_t mlock;
	pthread_t* threads;
	ruyi_spsc_list_t* dns_list;
} ruyi_dns_info_t;

static ruyi_dns_info_t s_dns_info;

void* _ruyi_dns_work_()
{
	while (atomic_load_explicit(&s_dns_info.running, memory_order_relaxed)) {
		ruyi_dns_t** dns;
		ruyi_mutex_lock(&s_dns_info.mlock);
		while ((dns = ruyi_spsc_list_pop(s_dns_info.dns_list)) == NULL) {
			ruyi_mutex_wait(&s_dns_info.mlock);
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
		ruyi_socket_dns_result(*dns);
		RUYI_MEM_FREE(&dns);
	}

	return NULL;
}

void ruyi_dns_init()
{
	ruyi_mutex_init(&s_dns_info.mlock);
	s_dns_info.threads = RUYI_MEM_ALLOC(sizeof(pthread_t) * DNS_THREAD_COUNT);
	s_dns_info.dns_list = ruyi_spsc_list_create(sizeof(ruyi_dns_t*));
	atomic_store_explicit(&s_dns_info.running, true, memory_order_release);

	for(int32_t i = 0; i < DNS_THREAD_COUNT; i++) {
		int32_t ret = pthread_create(s_dns_info.threads + i, NULL, _ruyi_dns_work_, NULL);
		RUYI_EXIT_IF_MSG(ret != 0, "ruyi_dns_init(): dns work thread create failed: %s\n", strerror(ret));
	}
}

static inline void _dns_cleanup_()
{
	for(int32_t i = 0; i < DNS_THREAD_COUNT; i++) {
		pthread_join(s_dns_info.threads[i], NULL);
	}
	RUYI_MEM_FREE(&s_dns_info.threads);

	ruyi_mutex_destroy(&s_dns_info.mlock);
	ruyi_spsc_list_destroy(&s_dns_info.dns_list);
}

void* ruyi_dns_event()
{
	_dns_cleanup_();

	return NULL;
}

void ruyi_dns_notify_stop()
{
	atomic_store_explicit(&s_dns_info.running, false, memory_order_release);
}

void ruyi_dns_request(ruyi_dns_t* dns)
{
	RUYI_RETURN_IFUL(atomic_load_explicit(&s_dns_info.running, memory_order_relaxed) == false);

	ruyi_mutex_lock(&s_dns_info.mlock);
	ruyi_spsc_list_push(s_dns_info.dns_list, &dns);
	ruyi_mutex_signal(&s_dns_info.mlock);
	ruyi_mutex_unlock(&s_dns_info.mlock);
}
