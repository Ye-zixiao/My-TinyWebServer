#ifndef TIMERLIST_H_
#define TIMERLIST_H_

#include <time.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <queue>

struct heap_timer;

/* 用户数据 */
struct client_data {
	struct sockaddr_in cliaddr;
	heap_timer* timer;
	int sockfd;
};

/* 定时器 */
struct heap_timer {
	heap_timer(int delay) :_M_expire(time(nullptr) + delay) {}
	
	time_t _M_expire;
	void (*_M_callback)(client_data*);
	client_data* _M_user_data;
};

bool htp_comp(heap_timer* const& lhs, heap_timer* const& rhs);


/* 定时器最小堆 */
class timer_heap {
public:
	timer_heap() :_M_timer_heap(htp_comp) {}
	~timer_heap();

	void add_timer(heap_timer* timer);
	void del_timer(heap_timer* timer);
	void tick();

private:
	std::priority_queue<heap_timer*, std::vector<heap_timer*>,
		decltype(htp_comp)*> _M_timer_heap;
};


#endif // !TIMERLIST_H_