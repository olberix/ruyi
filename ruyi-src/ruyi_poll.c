#include "ruyi_poll.h"

#ifdef __linux__

	#include <sys/epoll.h>
	#include <stddef.h>

	int32_t ruyi_poll_create()
	{
		return epoll_create(1);
	}

	void ruyi_poll_close(int32_t pfd)
	{
		close(pfd);
	}

	int32_t ruyi_poll_add(int32_t pfd, int32_t sfd, void* ud)
	{
		struct epoll_event ev;
		ev.events = EPOLLIN;
		ev.data.ptr = ud;

		return epoll_ctl(pfd, EPOLL_CTL_ADD, sfd, &ev);
	}

	int32_t ruyi_poll_del(int32_t pfd, int32_t sfd)
	{
		return epoll_ctl(pfd, EPOLL_CTL_DEL, sfd , NULL);
	}

	int32_t ruyi_poll_ctl(int32_t pfd, int32_t sfd, void* ud, bool readable, bool writable)
	{
		struct epoll_event ev;
		ev.events = (readable ? EPOLLIN : 0) | (writable ? EPOLLOUT : 0);
		ev.data.ptr = ud;

		return epoll_ctl(pfd, EPOLL_CTL_ADD, sfd, &ev);
	}

#else /* kqueue */

	int32_t ruyi_poll_create()
	{

	}

	void ruyi_poll_close(int32_t pfd)
	{

	}

	int32_t ruyi_poll_add(int32_t pfd, int32_t sfd, void* ud)
	{

	}

	int32_t ruyi_poll_del(int32_t pfd, int32_t sfd)
	{

	}

	int32_t ruyi_poll_ctl(int32_t pfd, int32_t sfd, bool readable, bool writable)
	{
		
	}

#endif