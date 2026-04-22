#include "ruyi_poll.h"

#include <stddef.h>
#include <unistd.h>

#ifdef __linux__

	int32_t ruyi_poll_create()
	{
		return epoll_create(1);
	}

	int32_t ruyi_poll_close(int32_t pfd)
	{
		return close(pfd);
	}

	int32_t ruyi_poll_add(int32_t pfd, int32_t sfd, void* ud)
	{
		ruyi_poll_event_t ev;
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
		ruyi_poll_event_t ev;
		ev.events = (readable ? EPOLLIN : 0) | (writable ? EPOLLOUT : 0);
		ev.data.ptr = ud;

		return epoll_ctl(pfd, EPOLL_CTL_ADD, sfd, &ev);
	}

	int32_t ruyi_poll_wait(int32_t pfd, ruyi_poll_event_t* ev, int32_t count)
	{
		return epoll_wait(pfd, ev, count, 0);
	}

	bool ruyi_poll_event_readable(const ruyi_poll_event_t* ev)
	{
		return (ev->events & EPOLLIN) != 0;
	}

	bool ruyi_poll_event_writable(const ruyi_poll_event_t* ev)
	{
		return (ev->events & EPOLLOUT) != 0;
	}

	bool ruyi_poll_event_error(const ruyi_poll_event_t* ev)
	{
		return (ev->events & (EPOLLERR | EPOLLHUP)) != 0;
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