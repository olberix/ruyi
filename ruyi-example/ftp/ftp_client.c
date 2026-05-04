#include "ruyi_net.h"
#include "ruyi.h"
#include "md5.h"
#include "ruyi_malloc.h"

#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

void free_send_str(void* str)
{
	RUYI_MEM_FREE(&str);
}

int main()
{
	ruyi_start();

	const char* filename = "2.mp4";
	char md5str1[33];
	char md5str2[33];
	file_md5(filename, md5str1);

	char* host = "127.0.0.7";
	char* port = "20619";
	ruyi_net_connect(host, port, IPPROTO_TCP);
	uint32_t conn_id;

	ruyi_net_msg_t* msg;
	while (true) {
		msg = ruyi_net_get_msg();
		if (msg && msg->ev == RUYI_NET_EVENT_CONNECT_ACTIVE && strcmp(msg->data.conn_act.hostname, host) == 0 && strcmp(msg->data.conn_act.service, port) == 0) {
			conn_id = msg->id;
			ruyi_net_destroy_msg(&msg);
			break;
		}
		ruyi_net_destroy_msg(&msg);
	}

    FILE* fp = fopen(filename, "rb");
	MD5_CTX ctx;
    MD5_Init(&ctx);
	size_t tn1 = 0, tn2 = 0;
	struct stat st;
	stat(filename, &st);
	tn1 = st.st_size;
	int32_t p = 10;
	while (true) {
		int32_t times = 1 + rand() % 10;
		while (ftell(fp) != (ssize_t)tn1 && --times >= 0) {
			int32_t len = 1 + rand() % (64 * 1024);
			char* str = RUYI_MEM_ALLOC(len);
			size_t n = fread(str, 1, len, fp);
			if (n > 0) {
				ruyi_net_send(conn_id, str, n, free_send_str);
			}
			else if (n == 0) {
				break;
			}
			else {
				fclose(fp);
				exit(EXIT_FAILURE);
			}
		}

		times = 1 + rand() % 10;
		while (--times >= 0) {
			msg = ruyi_net_get_msg();
			if (!msg) {
				break;
			}
			if (msg->id == conn_id && msg->ev == RUYI_NET_EVENT_READ) {
				MD5_Update(&ctx, (const uint8_t*)msg->data.read.rstr, msg->data.read.len);
				tn2 += msg->data.read.len;
				if (tn2 * p >= tn1) {
					printf("--------------------%d%%\n", (11 - p) * 10);
					p--;
				}
			}
			ruyi_net_destroy_msg(&msg);
		}
		if (tn1 == tn2) {
			MD5_Final(&ctx, md5str2);
			ruyi_net_close(conn_id, SHUT_RDWR);
			break;
		}

		// static uint32_t timess[6] = {0, 5000, 50000, 100000, 1000000, 2000000};
		// int32_t idx = rand() % 6;
		// time_t s = timess[idx] / 1000000;
		// struct timespec ts = {
		// 	.tv_sec = s,
		// 	.tv_nsec = (timess[idx] - s * 1000000) * 1000
		// };
		// nanosleep(&ts, NULL);
	}

	fclose(fp);

	printf("1: %s\n", md5str1);
	printf("2: %s\n", md5str2);
	if (strcmp(md5str1, md5str2) != 0) {
		printf("error\n");
	}
	else {
		printf("success\n");
	}

	ruyi_stop();
}
