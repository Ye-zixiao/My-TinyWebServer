#ifndef SYNCHRONIZE_H_
#define SYNCHRONIZE_H_

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class Locker {
public:
	Locker() {
		if (pthread_mutex_init(&_M_locker, nullptr) != 0)
			throw std::exception();
	}
	~Locker() { pthread_mutex_destroy(&_M_locker); }

	bool lock() { return pthread_mutex_lock(&_M_locker) == 0; }
	bool unlock() { return pthread_mutex_unlock(&_M_locker) == 0; }
	pthread_mutex_t* get() { return &_M_locker; }

private:
	pthread_mutex_t _M_locker;
};

class Sem {
public:
	explicit Sem(int initval) {
		if (sem_init(&_M_sem, PTHREAD_PROCESS_PRIVATE, initval) != 0)
			throw std::exception();
	}
	~Sem() { sem_destroy(&_M_sem); }

	bool wait() { return sem_wait(&_M_sem) == 0; }
	bool post() { return sem_post(&_M_sem) == 0; }

private:
	sem_t _M_sem;
};

class Cond {
public:
	Cond() {
		if (pthread_cond_init(&_M_cond, nullptr) != 0)
			throw std::exception();
	}
	~Cond() { pthread_cond_destroy(&_M_cond); }

	bool wait(pthread_mutex_t* mutex) {
		return pthread_cond_wait(&_M_cond, mutex) == 0;
	}
	bool signal() { return pthread_cond_signal(&_M_cond) == 0; }
	bool broadcast() { return pthread_cond_broadcast(&_M_cond) == 0; }

private:
	pthread_cond_t _M_cond;
};

#endif // !SYNCHRONIZE_H_