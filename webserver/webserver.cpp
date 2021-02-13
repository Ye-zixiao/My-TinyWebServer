#include "webserver.h"
#include <cstring>
#include <assert.h>
#include <signal.h>


WebServer::WebServer() {
	_M_http_tasks = new http_task[MAX_FD];
	_M_client_data = new client_data[MAX_FD];

	_M_root = new char[256];
	getcwd(_M_root, 256);
	strcat(_M_root, "/root");
}


WebServer::~WebServer() {
	close(_M_epollfd);
	close(_M_listenfd);
	close(_M_pipefd[1]);
	close(_M_pipefd[0]);
	delete[] _M_http_tasks;
	delete[] _M_client_data;
	delete _M_threadpool;
}


void WebServer::init(int port, const std::string& user, const std::string& passwd,
		const std::string& database, int log_write, int opt_linger, size_t sql_num,
		int nthread, int closelog) {
	_M_port = port;
	_M_user = user;
	_M_passwd = passwd;
	_M_database = database;
	_M_sql_num = sql_num;
	_M_nthread = nthread;
	_M_log_write = log_write;
	_M_linger_opt = opt_linger;
	_M_close_log = closelog;
}


//初始化日志设施
void WebServer::log_write() {
	if (0 == _M_close_log) {
		if (1 == _M_log_write)
			Log::getinstance()->init("./ServerLog", true);
		else
			Log::getinstance()->init("./ServerLog", false);
	}
}


//创建Mysql数据库连接库
void WebServer::sql_pool() {
	_M_sql_connpool = sql_connpool::get_instance();
	_M_sql_connpool->init("localhost", _M_user, _M_passwd,
		_M_database, 3306, _M_sql_num, _M_close_log);
	_M_http_tasks->initmysql_result(_M_sql_connpool);
}


//创建线程库
void WebServer::thread_pool() {
	_M_threadpool = new threadpool<http_task>(_M_sql_connpool, _M_nthread);
}


//创建监听套接字、epoll对象、并注册信号处理程序、管道
void WebServer::event_listen() {
	int ret = 0, on = 1;

	_M_listenfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(_M_listenfd != -1);
	if (_M_linger_opt == 1) {
		struct linger tmp { 1, 1 };
		setsockopt(_M_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}

	struct sockaddr_in srvaddr;
	bzero(&srvaddr, sizeof(srvaddr));
	srvaddr.sin_family = AF_INET;
	srvaddr.sin_port = htons(_M_port);
	srvaddr.sin_addr.s_addr = INADDR_ANY;
	setsockopt(_M_listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	ret = bind(_M_listenfd, (const struct sockaddr*)&srvaddr, sizeof(srvaddr));
	assert(ret != -1);
	ret = listen(_M_listenfd, 5);
	assert(ret != -1);

	_M_epollfd = epoll_create(5);
	assert(_M_epollfd != -1);
	addfd(_M_epollfd, _M_listenfd, false);
	http_task::_M_epollfd = _M_epollfd;

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, _M_pipefd);
	assert(ret != -1);
	setnonblocking(_M_pipefd[1]);
	addfd(_M_epollfd, _M_pipefd[0], false);
	set_static_fds(_M_epollfd, _M_pipefd);

	addsig(SIGPIPE, SIG_IGN);
	addsig(SIGALRM, sig_handler, false);
	addsig(SIGTERM, sig_handler, false);
	addsig(SIGINT, sig_handler, false);
	alarm(TIMESLOT);
}


//为新连接套接字创建一个定时器加入到定时器堆中，并记录用户数据
void WebServer::timer(int connfd, const struct sockaddr_in& cliaddr) {
	_M_http_tasks[connfd].init(connfd, cliaddr, _M_root, _M_close_log, _M_user, _M_passwd, _M_database);

	_M_client_data[connfd].cliaddr = cliaddr;
	_M_client_data[connfd].sockfd = connfd;
	heap_timer* timer = new heap_timer(3 * TIMESLOT);
	timer->_M_user_data = &_M_client_data[connfd];
	timer->_M_callback = callback;
	_M_client_data[connfd].timer = timer;

	extern_timer_heap.add_timer(timer);
}


//调整已连接套接字的定时器，使旧有的定时器失效，重新加入一个新定时器
void WebServer::adjust_timer(heap_timer* timer) {
	extern_timer_heap.del_timer(timer);
	int sockfd = timer->_M_user_data->sockfd;
	heap_timer* newtimer = new heap_timer(3 * TIMESLOT);
	newtimer->_M_callback = callback;
	newtimer->_M_user_data = &_M_client_data[sockfd];
	_M_client_data[sockfd].timer = newtimer;
	extern_timer_heap.add_timer(newtimer);
	std::cout << "adjust timer for " <<
		sock_ntop((const struct sockaddr*)(&newtimer->_M_user_data->cliaddr), 
			sizeof(struct sockaddr_in)) << std::endl;
	LOG_INFO("%s", "adjust timer once");
}


void WebServer::deal_timer(heap_timer* timer, int sockfd) {
	timer->_M_callback(&_M_client_data[sockfd]);
	if (timer) extern_timer_heap.del_timer(timer);
	LOG_INFO("close fd %d", _M_client_data[sockfd].sockfd);
}


void WebServer::handle_newconn() {
	struct sockaddr_in cliaddr;
	socklen_t clilen = sizeof(cliaddr);
	int connfd;
	while ((connfd = accept(_M_listenfd, (struct sockaddr*)&cliaddr, &clilen)) != -1) {
		if (http_task::_M_user_cnt >= MAX_FD) {
			show_error(connfd, "Internal server busy");
			LOG_ERROR("%s", "Internal server busy");
			break;
		}
		timer(connfd, cliaddr);

		const char* cliaddr_str = sock_ntop((const struct sockaddr*)&cliaddr, clilen);
		LOG_INFO("new connection from %s", cliaddr_str);
		std::cout << "new connection from " << cliaddr_str << std::endl;
	}
	if (connfd == -1)
		LOG_ERROR("%s: errno is %d", "accept error", errno);
}


bool WebServer::handle_signal(bool& timeout, bool& stop_server) {
	int ret = 0; char signals[1024];
	if ((ret = read(_M_pipefd[0], signals, sizeof(signals))) <= 0)
		return false;
	for (int i = 0; i < ret; ++i) {
		switch (signals[i])
		{
		case SIGALRM:
			timeout = true;
			break;
		case SIGINT:case SIGQUIT:case SIGTERM:
			stop_server = true;
			break;
		default:
			break;
		}
	}
	return true;
}


void WebServer::handle_read(int sockfd) {
	heap_timer* timer = _M_client_data[sockfd].timer;
	if (_M_http_tasks[sockfd].read_once()) {
		LOG_INFO("read from client(%s)", inet_ntoa(_M_http_tasks[sockfd].getcliaddr()->sin_addr));
		_M_threadpool->append(_M_http_tasks + sockfd);
		if (timer) adjust_timer(timer);
	}
	else deal_timer(timer, sockfd);
}


void WebServer::handle_write(int sockfd) {
	heap_timer* timer = _M_client_data[sockfd].timer;
	if (_M_http_tasks[sockfd].write()) {
		LOG_INFO("send data to the client(%s)", inet_ntoa(_M_http_tasks[sockfd].getcliaddr()->sin_addr));
		if (timer) adjust_timer(timer);
	}
	else deal_timer(timer, sockfd);
}


void WebServer::event_loop() {
	bool timeout = false;
	bool stop_server = false;

	while (!stop_server) {
		int nret = epoll_wait(_M_epollfd, _M_events, MAX_EVENT_NUM, -1);
		if (nret == -1 && errno != EINTR) {
			LOG_ERROR("%s", "epoll_wait error");
			break;
		}

		for (int i = 0; i < nret; ++i) {
			int sockfd = _M_events[i].data.fd;

			if (sockfd == _M_listenfd) {
				handle_newconn();
			}
			else if (_M_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
				heap_timer* timer = _M_client_data[sockfd].timer;
				deal_timer(timer, sockfd);
			}
			else if ((sockfd == _M_pipefd[0]) && (_M_events[i].events & EPOLLIN)) {
				if (!handle_signal(timeout, stop_server))
					LOG_ERROR("%s", "dealclientdata error");
			}
			else if (_M_events[i].events & EPOLLIN) {
				handle_read(sockfd);
			}
			else if (_M_events[i].events & EPOLLOUT) {
				handle_write(sockfd);
			}
		}
		if (timeout) {
			timer_handler();
			LOG_INFO("%s", "timer tick");
			timeout = false;
		}
	}

	std::cout << "\nserver is closing..." << std::endl;
}