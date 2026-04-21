#ifndef RUYI_POLL_H
#define RUYI_POLL_H

#include <stdint.h>
#include <stdbool.h>

int32_t ruyi_poll_create();
void ruyi_poll_close(int32_t pfd);

int32_t ruyi_poll_add(int32_t pfd, int32_t sfd, void* ud);
int32_t ruyi_poll_del(int32_t pfd, int32_t sfd);
int32_t ruyi_poll_ctl(int32_t pfd, int32_t sfd, void*, bool readable, bool writable);

int32_t ruyi_poll_wait(int32_t pfd, int32_t sfd, void*, bool readable, bool writable);

#endif
