#include "./webserver/webserver.h"
#include <libgen.h>


int main(int argc, char* argv[])
{
	int port = 12000;
	int nthread = 4;
	int sql_num = 8;
	int close_log = 0;
	char ch;

	opterr = 0;
	while ((ch = getopt(argc, argv, "hp:n:s:l:")) != -1) {
		switch (ch) {
		case 'h':
			std::cerr << "usage: " << basename(argv[0]) <<
				" [-p port] [-n threads] [-s sql_num] [-l close_log]"
				<< std::endl;
			exit(EXIT_FAILURE);
		case 'p':
			port = atoi(optarg);
			break;
		case 'n':
			nthread = atoi(optarg);
			break;
		case 's':
			sql_num = atoi(optarg);
			break;
		case 'l':
			close_log = atoi(optarg);
			break;
		case '?':
			std::cerr << "bad arg: " << optopt << std::endl;
			exit(EXIT_FAILURE);
		}
	}

	WebServer server;
	server.init(port, "root", "yxhhll19961105", "yourdb", 1, 1, sql_num, nthread, close_log);
	//初始化日志组件
	server.log_write();
	//创建数据库连接池
	server.sql_pool();
	//创建工作线程池
	server.thread_pool();
	//创建监听套接字、用于统一事件源的管道
	server.event_listen();
	//进入事件循环
	server.event_loop();
	exit(EXIT_SUCCESS);
}
