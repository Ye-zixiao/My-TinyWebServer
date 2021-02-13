#ifndef LOG_H_
#define LOG_H_

#include <string>
#include <iostream>
#include <pthread.h>
#include <cstdarg>
#include "log_queue.h"

class Log {
public:
	static constexpr size_t BUFSIZE = 4096;
	static constexpr size_t LOG_QUEUE_SZ = 1000;
	static constexpr size_t DEFAULT_SPLICT_LINES = 50000;
	enum { DEBUG = 0, INFO, WARN, ERROR };

	static Log* getinstance() {
		static Log loginstance;
		return &loginstance;
	}
	~Log() { if (_M_fp) fclose(_M_fp); }

	bool init(const char* filename, bool closelog, size_t bufsz = BUFSIZE,
		size_t split_lines = DEFAULT_SPLICT_LINES, size_t logqueuesz = LOG_QUEUE_SZ);
	void write_log(int level, const char* format, ...);
	void fflush() { ::fflush(_M_fp); }

	//日志处理线程函数
	static void* fllush_log_thread(void* args) {
		return Log::getinstance()->async_write_log();
	}

private:
	Log() :_M_curlines(0) {}

	void* async_write_log() {
		std::string logstr;
		while (_M_logqueue->pop(logstr)) {
			//互斥量的操作对于日志处理线程是否有必要？
			fputs(logstr.c_str(), _M_fp);
		}
		return nullptr;
	}

public:
	static bool _M_close_log;

private:
	log_queue<std::string>* _M_logqueue;
	Locker _M_locker;	//维护日志消息队列的原子性
	FILE* _M_fp;

	char _M_logfile[256];
	long long _M_split_lines;
	long long _M_curlines;
	size_t _M_bufsize;
	char* _M_buf;
	int _M_today;
};

#define LOG_DEBUG(format, ...)	\
	if(Log::_M_close_log) {		\
		Log::getinstance()->write_log(Log::DEBUG, format, ##__VA_ARGS__);	\
		Log::getinstance()->fflush();	\
	}
#define LOG_INFO(format, ...)	\
	if(Log::_M_close_log) {		\
		Log::getinstance()->write_log(Log::INFO, format, ##__VA_ARGS__);	\
		Log::getinstance()->fflush();	\
	}
#define LOG_WARN(format, ...)	\
	if(Log::_M_close_log) {		\
		Log::getinstance()->write_log(Log::WARN, format, ##__VA_ARGS__);	\
		Log::getinstance()->fflush();	\
	}
#define LOG_ERROR(format, ...)	\
	if(Log::_M_close_log) {		\
		Log::getinstance()->write_log(Log::ERROR, format, ##__VA_ARGS__);	\
		Log::getinstance()->fflush();	\
	}


#endif // !LOG_H_