#ifndef MYUTILS_H_
#define MYUTILS_H_

#include "../timer/timer_heap.h"

#define TIMESLOT 3	//使用宏的缺点是自定义性差

extern timer_heap extern_timer_heap;//其实应该避免使用全局变量的

void addsig(int signo, void(*handler)(int), bool restart = true);
void addfd(int epfd, int fd, bool oneshot = false);
void modfd(int epfd, int fd, int ev);
void removefd(int epfd, int fd);
int setnonblocking(int fd);

void sig_handler(int signo);
void timer_handler();
void show_error(int connfd, const char* info);
void callback(client_data* user_data);

char* sock_ntop(const struct sockaddr* sockaddr, socklen_t addrlen);
const char* currtime(const char* fmt);
void set_static_fds(int epfd, int pipefd[2]);


#endif // !MYUTILS_H_
