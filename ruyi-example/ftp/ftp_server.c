#include "ruyi_net.h"
#include "ruyi.h"
#include "ruyi_malloc.h"

#include <unistd.h>
#include <time.h>

void free_send_str(void* str)
{
	RUYI_MEM_FREE(&str);
}

int main()
{
	ruyi_start();
	
	char* host = "127.0.0.1";
	char* port = "16888";
	ruyi_net_listen(host, port, IPPROTO_TCP);

	ruyi_net_msg_t* msg;
	while (true) {
		static uint32_t timess[6] = {0, 5000, 50000, 100000, 1000000, 2000000};
		int32_t idx = rand() % 6;
		time_t s = timess[idx] / 1000000;
		struct timespec ts = {
			.tv_sec = s,
			.tv_nsec = (timess[idx] - s * 1000000) * 1000
		};
		nanosleep(&ts, NULL);
		
		msg = ruyi_net_get_msg();
		if (msg && msg->ev == RUYI_NET_EVENT_READ) {
			char* str = RUYI_MEM_ALLOC(msg->data.read.len);
			memcpy(str, msg->data.read.rstr, msg->data.read.len);
			ruyi_net_send(msg->id, str, msg->data.read.len, free_send_str);
		}
		ruyi_net_destroy_msg(&msg);
	}

	ruyi_stop();

	return 0;
}
