#include "ruyi_net.h"
#include "ruyi.h"
#include "ruyi_malloc.h"

#include <unistd.h>

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
		msg = ruyi_net_get_msg();
		if (msg && msg->ev == RUYI_NET_EVENT_READ) {
			char* str = RUYI_MEM_ALLOC(msg->data.read.len);
			memcpy(str, msg->data.read.rstr, msg->data.read.len);
			ruyi_net_send(msg->id, str, msg->data.read.len, free_send_str);
			printf("%.*s\n", msg->data.read.len, str);
		}
		ruyi_net_destroy_msg(&msg);
	}

	ruyi_stop();

	return 0;
}
