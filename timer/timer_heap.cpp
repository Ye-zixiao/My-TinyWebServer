#include "timer_heap.h"


bool htp_comp(heap_timer* const& lhs, heap_timer* const& rhs) {
	return lhs->_M_expire > rhs->_M_expire;
}


timer_heap::~timer_heap() {
	while (!_M_timer_heap.empty()) {
		heap_timer* timer = _M_timer_heap.top();
		_M_timer_heap.pop();
		delete timer;
	}
}


void timer_heap::add_timer(heap_timer* timer) {
	if (timer) _M_timer_heap.push(timer);
}


void timer_heap::del_timer(heap_timer* timer) {
	if (timer) timer->_M_callback = nullptr;
}


void timer_heap::tick() {
	time_t now = time(nullptr);
	while (!_M_timer_heap.empty()) {
		heap_timer* timer = _M_timer_heap.top();
		if (timer->_M_expire > now) break;
		if (timer->_M_callback) timer->_M_callback(timer->_M_user_data);
		_M_timer_heap.pop();
		delete timer;
	}
}