#ifndef WEBSERVER_H_
#define WEBSERVER_H_

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>

#include "../mysql/sql_connpool.h"
#include "../http/http_task.h"
#include "../threadpool/threadpool.h"
#include "../timer/timer_heap.h"
#include "../myutils/myutils.h"
#include "../log/log.h"

class WebServer {
public:
	static constexpr int MAX_FD = 65536;
	static constexpr int MAX_EVENT_NUM = 10000;

	WebServer();
	~WebServer();

	void init(int port, const std::string& user, const std::string& passwd,
		const std::string& database, int log_write, int opt_linger, size_t sql_num,
		int nthread, int closelog);
	void log_write();
	void sql_pool();
	void thread_pool();
	void event_listen();
	void event_loop();
	void timer(int connfd, const struct sockaddr_in& cliaddr);
	void adjust_timer(heap_timer* timer);
	void deal_timer(heap_timer* timer, int sockfd);

	void handle_newconn();
	bool handle_signal(bool& timeout, bool& stop_server);
	void handle_read(int sockfd);
	void handle_write(int sockfd);

public:
	int _M_port;
	char* _M_root;
	int _M_log_write;
	int _M_close_log;

	struct epoll_event _M_events[MAX_EVENT_NUM];
	int _M_pipefd[2];
	int _M_epollfd;
	int _M_listenfd;
	int _M_linger_opt;

	/* Mysql连接池相关信息 */
	sql_connpool* _M_sql_connpool;
	std::string _M_user;
	std::string _M_passwd;
	std::string _M_database;
	int _M_sql_num;

	/* HTTP任务数组 */
	http_task* _M_http_tasks;

	/* 工作线程池 */
	threadpool<http_task>* _M_threadpool;
	int _M_nthread;
	
	/* 定时器需要使用到的用户数据表 */
	client_data* _M_client_data;
};


#endif // !WEBSERVER_H_
