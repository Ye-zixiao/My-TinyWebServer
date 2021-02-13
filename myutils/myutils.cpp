#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include "../myutils/myutils.h"
#include "../http/http_task.h"

timer_heap extern_timer_heap;
/* 之所以需要使用下面两个静态变量，主要是为了针对timer_handler()、
	sig_hangler()和callback()考虑，其实这里整合到webserver.h这部分更好 */
static int _M_pipefd[2] = { -1,-1 };
static int _M_epfd = -1;


void addsig(int signo, void(*handler)(int), bool restart) {
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	if (restart) sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(signo, &sa, nullptr) != -1);
}


void addfd(int epfd, int fd, bool oneshot) {
	struct epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
	if (oneshot)event.events |= EPOLLONESHOT;
	epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}


void removefd(int epfd, int fd) {
	epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
	close(fd);
}


void modfd(int epfd, int fd, int ev) {
	struct epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
}


void show_error(int connfd, const char* info) {
	write(connfd, info, strlen(info));
	close(connfd);
}


void timer_handler() {
	extern_timer_heap.tick();
	alarm(TIMESLOT);
}


void sig_handler(int signo) {
	int errno_save = errno;
	write(_M_pipefd[1], &signo, 1);
	errno = errno_save;
}


int setnonblocking(int fd) {
	int oldopt = fcntl(fd, F_GETFL);
	int newopt = oldopt | O_NONBLOCK;
	fcntl(fd, F_SETFL, newopt);
	return oldopt;
}


void callback(client_data* user_data) {
	assert(user_data);
	epoll_ctl(_M_epfd, EPOLL_CTL_DEL, user_data->sockfd, nullptr);
	close(user_data->sockfd);
	std::cout << "close connection for " <<
		sock_ntop((const struct sockaddr*)&user_data->cliaddr,
			sizeof(user_data->cliaddr)) << std::endl;
	http_task::_M_user_cnt--;
}


const char* currtime(const char* fmt) {
	static char timestr[64];
	struct tm* ptm;
	time_t now;

	time(&now);
	ptm = localtime(&now);
	strftime(timestr, 64, fmt ? fmt : "%T", ptm);
	return timestr;
}


void set_static_fds(int epfd, int pipefd[2]) {
	_M_epfd = epfd;
	_M_pipefd[0] = pipefd[0];
	_M_pipefd[1] = pipefd[1];
}


char* sock_ntop(const struct sockaddr* sockaddr, socklen_t addrlen) {
	static char buf[128];
	char port[8];

	if (sockaddr->sa_family == AF_INET) {
		const struct sockaddr_in* sin = (const struct sockaddr_in*)sockaddr;

		if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) == NULL)
			return NULL;
		if (ntohs(sin->sin_port) != 0) {
			snprintf(port, sizeof(port), ":%d", ntohs(sin->sin_port));
			strcat(buf, port);
		}
		return buf;
	}
	else if (sockaddr->sa_family == AF_INET6) {
		const struct sockaddr_in6* sin6 = (const struct sockaddr_in6*)sockaddr;

		buf[0] = '[';
		if (inet_ntop(AF_INET6, &sin6->sin6_addr, buf + 1, sizeof(buf) - 1) == NULL)
			return NULL;
		if (ntohs(sin6->sin6_port) != 0) {
			snprintf(port, sizeof(port), "]:%d", htons(sin6->sin6_port));
			strcat(buf, port);
			return buf;
		}
		return buf + 1;
	}
	//其他协议暂时不实现
	errno = EAFNOSUPPORT;
	return NULL;
}
