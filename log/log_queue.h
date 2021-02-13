#ifndef LOG_QUEUE_H_
#define LOG_QUEUE_H_

#include "../synchronize/synchronize.h"

/* 日志消息队列，主线程和工作线程向队列中写入新的日志消息，
	日志消息处理线程从队列中取出消息并将其写入到日志文件中 */

template<typename LogMsg>
class log_queue {
public:
	log_queue(size_t maxsz = 1000) :
		_M_size(0), _M_front(-1), _M_back(-1) {
		if (maxsz <= 0) throw std::exception();

		_M_maxsz = maxsz;
		_M_msgarr = new LogMsg[maxsz];
	}
	~log_queue() {
		_M_locker.lock();
		if (_M_msgarr) delete[] _M_msgarr;
		_M_locker.unlock();
	}

	bool full() {
		_M_locker.lock();
		if (_M_size == _M_maxsz) {
			_M_locker.unlock();
			return true;
		}
		_M_locker.unlock();
		return false;
	}

	bool empty() { 
		_M_locker.lock();
		if (_M_size == 0) {
			_M_locker.unlock();
			return true;
		}
		_M_locker.unlock();
		return false;
	}

	void clear() {
		_M_locker.lock();
		_M_size = _M_front = _M_back = -1;
		_M_locker.unlock();
	}

	bool front(LogMsg& msg) {
		_M_locker.lock();
		if (_M_size == 0) {
			_M_locker.unlock();
			return false;
		}
		msg = _M_msgarr[_M_front];
		_M_locker.unlock();
		return true;
	}

	bool back(LogMsg& msg) {
		_M_locker.lock();
		if (_M_size == 0) {
			_M_locker.unlock();
			return false;
		}
		msg = _M_msgarr[_M_back];
		_M_locker.unlock();
		return true;
	}

	size_t size() {
		size_t tmp = 0;
		_M_locker.lock();
		tmp = _M_size;
		_M_locker.unlock();
		return tmp;
	}

	size_t max_size() { return _M_maxsz; }

	bool push(const LogMsg& msg) {
		_M_locker.lock();
		if (_M_size == _M_maxsz) {
			_M_locker.unlock();
			_M_cond.broadcast();
			return false;
		}
		_M_back = (_M_back + 1) % _M_maxsz;
		_M_msgarr[_M_back] = msg;
		_M_size++;
		_M_locker.unlock();
		_M_cond.signal();
		return true;
	}

	bool pop(LogMsg& msg) {
		_M_locker.lock();
		while (_M_size <= 0) {
			if (!_M_cond.wait(_M_locker.get())) {
				_M_locker.unlock();
				return false;
			}
		}
		_M_front = (_M_front + 1) % _M_maxsz;
		msg = _M_msgarr[_M_front];
		_M_size--;
		_M_locker.unlock();
		return true;
	}

private:
	Locker _M_locker;
	Cond _M_cond;
	LogMsg* _M_msgarr;
	size_t _M_size;
	size_t _M_maxsz;
	size_t _M_front;
	size_t _M_back;
};


#endif // !LOG_QUEUE_H_
