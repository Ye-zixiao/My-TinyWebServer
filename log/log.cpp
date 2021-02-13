#include "log.h"
#include <cstring>
#include <ctime>
#include <sys/time.h>

bool Log::_M_close_log = false;

bool Log::init(const char* filename, bool closelog,
	size_t bufsz, size_t split_lines, size_t logqueuesz) {
	/* 创建日志队列和日志处理线程 */
	pthread_t tid;
	_M_logqueue = new log_queue<std::string>(logqueuesz);
	pthread_create(&tid, nullptr, Log::fllush_log_thread, nullptr);
	std::cout << "log thread created" << std::endl;
	pthread_detach(tid);

	_M_close_log = closelog;
	_M_bufsize = bufsz;
	_M_buf = new char[_M_bufsize];
	memset(_M_buf, '\0', _M_bufsize);
	_M_split_lines = split_lines;

	//获取当前时间
	struct tm mytm;
	time_t now = time(nullptr);
	localtime_r(&now, &mytm);
	_M_today = mytm.tm_mday;

	//创建日志文件
	char logfile_fullname[256] = { 0 };
	const char* p = strrchr(filename, '/');
	if (p)
		snprintf(logfile_fullname, 255, "%s_%d_%02d_%02d",
			filename, mytm.tm_year + 1900, mytm.tm_mon, mytm.tm_mday);
	else {
		strncpy(_M_logfile, filename, 256);
		snprintf(logfile_fullname, 255, "%s_%d_%02d_%02d",
			filename, mytm.tm_year + 1900, mytm.tm_mon, mytm.tm_mday);
	}
	if ((_M_fp = fopen(logfile_fullname, "a")) == nullptr)
		return false;
	return true;
}


/* 由主线程或者工作线程向日志消息队列添加一个日志消息 */
void Log::write_log(int level, const char* fmt, ...) {
	struct tm mytm;
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	localtime_r(&tv.tv_sec, &mytm);
	char s[16] = { 0 };
	switch (level) {
	case DEBUG:
		strcpy(s, "[debug]:"); break;
	case INFO:
		strcpy(s, "[info]:"); break;
	case WARN:
		strcpy(s, "[warn]:"); break;
	case ERROR:
		strcpy(s, "[error]:"); break;
	default:
		strcpy(s, "[info]:"); break;
	}

	/* 判断是否需要生成新的日志文件 */
	_M_locker.lock();
	_M_curlines++;
	if (_M_today != mytm.tm_mday || _M_curlines % _M_split_lines == 0) {
		char newlogfilename[256] = { 0 };
		::fflush(_M_fp);
		fclose(_M_fp);
		if (_M_today != mytm.tm_mday) {
			_M_today = mytm.tm_mday;
			_M_curlines = 0;
			snprintf(newlogfilename, 255, "%s_%d_%02d_%02d",
				_M_logfile, mytm.tm_year + 1900, mytm.tm_mon, mytm.tm_mday);
		}
		else {
			snprintf(newlogfilename, 255, "%s_%d_%02d_%02d.%lld",
				_M_logfile, mytm.tm_year + 1900, mytm.tm_mon, mytm.tm_mday,
				_M_curlines / _M_split_lines);
		}
		_M_fp = fopen(newlogfilename, "a");
	}
	_M_locker.unlock();
	
	/* 向日志队列加入新的日志消息 */
	va_list valst;
	va_start(valst, fmt);
	std::string logstr;

	_M_locker.lock();
	int n = snprintf(_M_buf, BUFSIZE, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
		mytm.tm_year + 1900, mytm.tm_mon, mytm.tm_mday, mytm.tm_hour,
		mytm.tm_min, mytm.tm_sec, tv.tv_usec, s);
	int m = vsnprintf(_M_buf + n, _M_bufsize - 1, fmt, valst);
	_M_buf[m + n] = '\n';
	_M_buf[m + n + 1] = '\0';
	logstr = _M_buf;
	_M_locker.unlock();

	_M_logqueue->push(logstr);
	va_end(valst);
}