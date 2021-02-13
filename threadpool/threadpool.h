#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <queue>
#include <iostream>
#include <pthread.h>
#include <exception>
#include "../synchronize/synchronize.h"
#include "../mysql/sql_connpool.h"

/* 使用模拟Proactor模式的半同步/半反应堆线程池 */

template<typename Task>
class threadpool {
public:
	threadpool(sql_connpool* cpool, size_t nthread = 4, size_t max_ntask = 10000);
	~threadpool();

	bool append(Task* task);

private:
	static void* workthread(void* args);
	void run();

private:
	std::queue<Task*> _M_workqueue;
	size_t _M_max_ntask;
	pthread_t* _M_threads;
	size_t _M_nthread;
	Locker _M_locker;
	Cond _M_cond;

	sql_connpool* _M_sql_connpool;
};


template<typename Task>
threadpool<Task>::threadpool(sql_connpool* pool, size_t nthread, size_t max_ntask) :
	_M_max_ntask(max_ntask), _M_nthread(nthread), _M_sql_connpool(pool) {
	if (nthread <= 0 || max_ntask <= 0) throw std::exception();
	_M_threads = new pthread_t[nthread];

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	for (size_t i = 0; i < nthread; ++i) {
		if (pthread_create(&_M_threads[i], &attr, workthread, this) != 0) {
			delete[] _M_threads;
			throw std::exception();
		}
		std::cout << "thread " << i << " created" << std::endl;
	}
	pthread_attr_destroy(&attr);
}

template<typename Task>
inline threadpool<Task>::~threadpool() { delete[] _M_threads; }

template<typename Task>
void* threadpool<Task>::workthread(void* args) {
	threadpool<Task>* pool = reinterpret_cast<threadpool<Task>*>(args);
	pool->run();
	return pool;
}

template<typename Task>
inline bool threadpool<Task>::append(Task* task) {
	_M_locker.lock();
	if (_M_workqueue.size() >= _M_max_ntask) {
		_M_locker.unlock();
		return false;
	}
	_M_workqueue.push(task);
	_M_locker.unlock();
	_M_cond.signal();
	return true;
}

template<typename Task>
void threadpool<Task>::run() {
	for (;;) {
		_M_locker.lock();
		while (_M_workqueue.empty())
			_M_cond.wait(_M_locker.get());
		Task* task = _M_workqueue.front();
		_M_workqueue.pop();
		_M_locker.unlock();
		if (task) {
			//先从数据库连接库中获取一个连接，并在处理完毕之后自动返还
			sqlconnRAII sqlconn(task->_M_mysql, _M_sql_connpool);
			//再进行具体的逻辑处理
			task->process();
		}
	}
}


#endif // !THREADPOOL_H_
