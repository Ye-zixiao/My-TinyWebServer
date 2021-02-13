#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <cstring>
#include <iostream>
#include <stdarg.h>
#include <unistd.h>
#include "http_task.h"
#include "../myutils/myutils.h"
#include "../log/log.h"

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "/usr/share/nginx/html";

//该哈希表是在服务器初始化时完成内容填充
std::unordered_map<std::string, std::string> _M_user;
//负责保护哈希表的原子性
Locker _M_user_locker;
int http_task::_M_user_cnt = 0;
int http_task::_M_epollfd = -1;


/* 在HTTP任务数组创建后第一时间将数据库中的内容记录到_M_user用户-密码哈希表中 */
void http_task::initmysql_result(sql_connpool* connpool) {
	MYSQL* mysql = nullptr;
	sqlconnRAII mysqlconn(mysql, connpool);

	if (mysql_query(mysql, "SELECT username,passwd FROM user"))
		LOG_ERROR("mysql_query error");
	MYSQL_RES* result = mysql_store_result(mysql);
	while (MYSQL_ROW row = mysql_fetch_row(result)) {
		std::string str_key(row[0]);
		std::string str_map(row[1]);
		_M_user[str_key] = str_map;
	}
}


void http_task::init(int sockfd, const struct sockaddr_in& cliaddr,
		char* root, int closelog, const std::string& user, 
		const std::string& passwd, const std::string& sqlname) {
	_M_sockfd = sockfd;
	_M_cliaddr = cliaddr;
	_M_close_log = closelog;
	_M_root = root;
	_M_user_cnt++;
	addfd(_M_epollfd, sockfd, true);
	strcpy(_M_sql_user, user.c_str());
	strcpy(_M_sql_passwd, passwd.c_str());
	strcpy(_M_sql_name, sqlname.c_str());
	init();
}


void http_task::init() {
	_M_mysql = nullptr;
	_M_bytes_have_send = 0;
	_M_bytes_to_send = 0;
	_M_check_state = CHECK_STATE_REQUESTLINE;
	_M_linger = false;
	_M_method = GET;
	_M_host = nullptr;
	_M_url = nullptr;
	_M_version = nullptr;
	_M_content_len = 0;
	_M_start_line = 0;
	_M_chkidx = 0;
	_M_rdidx = 0;
	_M_wridx = 0;
	_M_cgi = 0;
	memset(_M_readbuf, '\0', READ_BUFSIZE);
	memset(_M_writebuf, '\0', WRITE_BUFSIZE);
	memset(_M_real_file, '\0', FILENAME_LEN);
}


void http_task::close_conn(bool real_close) {
	if (real_close && (_M_sockfd != -1)) {
		std::cout << "close " << _M_sockfd << std::endl;
		removefd(_M_epollfd, _M_sockfd);
		_M_sockfd = -1;
		_M_user_cnt--;
	}
}


bool http_task::read_once() {
	if (_M_rdidx >= READ_BUFSIZE)
		return false;
	
	ssize_t nread;
	while ((nread = read(_M_sockfd, _M_readbuf + _M_rdidx, READ_BUFSIZE - _M_rdidx)) > 0)
		_M_rdidx += nread;
	if (nread == 0 || (nread == -1 && errno != EWOULDBLOCK))
		return false;
	return true;
}


http_task::LINE_STATUS http_task::parse_line() {
	char ch;
	for (; _M_chkidx < _M_rdidx; ++_M_chkidx) {
		ch = _M_readbuf[_M_chkidx];
		if (ch == '\r') {
			if ((_M_chkidx + 1) == _M_rdidx)
				return LINE_OPEN;
			else if (_M_readbuf[_M_chkidx + 1] == '\n') {
				_M_readbuf[_M_chkidx++] = '\0';
				_M_readbuf[_M_chkidx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		else if (ch == '\n') {
			if (_M_chkidx > 1 && (_M_readbuf[_M_chkidx - 1] == '\r')) {
				_M_readbuf[_M_chkidx - 1] = '\0';
				_M_readbuf[_M_chkidx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}
	return LINE_OPEN;
}


http_task::HTTP_CODE http_task::parse_request_line(char* text) {
	if (!(_M_url = strpbrk(text, " \t")))
		return BAD_REQUEST;

	*_M_url++ = '\0';
	char* method = text;
	if (strcasecmp(method, "GET") == 0)
		_M_method = GET;
	else if (strcasecmp(method, "POST") == 0) {
		_M_method = POST;
		_M_cgi = 1;
	}
	else return BAD_REQUEST;

	_M_url += strspn(_M_url, " \t");
	if (!(_M_version = strpbrk(_M_url, " \t")))
		return BAD_REQUEST;
	*_M_version++ = '\0';
	_M_version += strspn(_M_url, " \t");
	if (strcasecmp(_M_version, "HTTP/1.1") != 0)
		return BAD_REQUEST;
	if (strncasecmp(_M_url, "http://", 7) == 0) {
		_M_url += 7;
		_M_url = strchr(_M_url, '/');
	}
	else if (strncasecmp(_M_url, "https://", 8) == 0) {
		_M_url += 8;
		_M_url = strchr(_M_url, '/');
	}

	if (!_M_url || _M_url[0] != '/')
		return BAD_REQUEST;
	if (strcmp(_M_url, "/") == 0)
		strcat(_M_url, "index.html");
	_M_check_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}


http_task::HTTP_CODE http_task::parse_header(char* text) {
	if (text[0] == '\0') {
		if (_M_content_len != 0) {
			_M_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		return GET_REQUEST;
	}
	else if (strncasecmp(text, "Connection:", 11) == 0) {
		text += 11;
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0)
			_M_linger = true;
	}
	else if (strncasecmp(text, "Content-length:", 15) == 0) {
		text += 15;
		text += strspn(text, " \t");
		_M_content_len = atol(text);
	}
	else {
		LOG_INFO("unsupport header: %s", text);
	}
	return NO_REQUEST;
}


http_task::HTTP_CODE http_task::parse_content(char* text) {
	if (_M_rdidx >= (_M_content_len + _M_chkidx)) {
		text[_M_content_len] = '\0';
		_M_string = text;
		return GET_REQUEST;
	}
	return NO_REQUEST;
}


http_task::HTTP_CODE http_task::process_read() {
	LINE_STATUS line_status = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char* text = nullptr;

	while ((_M_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
		(line_status = parse_line()) == LINE_OK) {
		text = get_line();
		_M_start_line = _M_chkidx;
		LOG_INFO("%s", text);

		switch (_M_check_state) {
		case CHECK_STATE_REQUESTLINE: {
			if ((ret = parse_request_line(text)) == BAD_REQUEST)
				return BAD_REQUEST;
			break;
		}
		case CHECK_STATE_HEADER: {
			if ((ret = parse_header(text)) == BAD_REQUEST)
				return BAD_REQUEST;
			else if (ret == GET_REQUEST)
				return do_request();
			break;
		}
		case CHECK_STATE_CONTENT: {
			if (parse_content(text) == GET_REQUEST)
				return do_request();
			line_status = LINE_OPEN;
			break; 
		}
		default: 
			return INTERNAL_ERROR;
		}
	}
	return NO_REQUEST;
}


/* 负责在HTTP解析后的进一步处理，是服务器中的“后端”：
	对GET进行文件读取，对POST报文进行注册、登录等任务 */
http_task::HTTP_CODE http_task::do_request() {
	strcpy(_M_real_file, _M_root);
	size_t len = strlen(_M_root);
	const char* p = strrchr(_M_url, '/');
	char flag = *(p + 1);

	//处理HTTP POST报文：user=xxx&password=xxx
	if (_M_cgi && (flag == '2' || flag == '3')) {
		char m_url_real[128];
		strcpy(m_url_real, "/");
		strcat(m_url_real, p + 2);
		strncpy(_M_real_file + len, m_url_real, FILENAME_LEN - len - 1);

		int i, j;
		char name[128], passwd[128];
		for (i = 5; _M_string[i] != '&'; ++i)
			name[i - 5] = _M_string[i];
		name[i - 5] = '\0';
		for (j = 0, i = i + 10; _M_string[i] != '\0'; ++i, ++j)
			passwd[j] = _M_string[i];
		passwd[j] = '\0';

		//注册账号
		if (flag == '3') {
			char sql_insert[200];
			strcpy(sql_insert, "INSERT INTO user(username,passwd) VALUES(");
			strcat(sql_insert, "'");
			strcat(sql_insert, name);
			strcat(sql_insert, "', '");
			strcat(sql_insert, passwd);
			strcat(sql_insert, "')");
			if (_M_user.find(name) == _M_user.end()) {
				_M_user_locker.lock();
				int res = mysql_query(_M_mysql, sql_insert);
				_M_user[name] = passwd;
				_M_user_locker.unlock();

				if (!res) strcpy(_M_url, "/log.html");
				else strcpy(_M_url, "/registerError.html");
			}
			else strcpy(_M_url, "/registerError.html");
		}
		//登录检测
		else if (flag == '2') {
			if (_M_user.find(name) != _M_user.end() && _M_user[name] == passwd)
				strcpy(_M_url, "/welcome.html");
			else
				strcpy(_M_url, "/logError.html");
		}
	}

	if (flag == '0')
		strcpy(_M_real_file + len, "/log.html");
	else if (flag == '1')
		strcpy(_M_real_file + len, "/register.html");
	//下面的其实可以统一到文件GET请求中
	else if (flag == '4')
		strcpy(_M_real_file + len, "/picture.html");
	else if (flag == '5')
		strcpy(_M_real_file + len, "/video.html");
	else if (flag == '6')
		strcpy(_M_real_file + len, "/baidu.html");
	else
		strncpy(_M_real_file + len, _M_url, FILENAME_LEN - 1 - len);

	LOG_INFO("clinet request %s", _M_real_file);
	
	//获取指定文件
	if (stat(_M_real_file, &_M_file_stat) == -1)
		return NO_RESOURCE;
	if (!(_M_file_stat.st_mode & S_IROTH))
		return FORBIDDEN_REQUEST;
	if (S_ISDIR(_M_file_stat.st_mode))
		return BAD_REQUEST;
	int fd = open(_M_real_file, O_RDONLY);
	_M_fileaddr = mmap(nullptr, _M_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	return FILE_REQUEST;
}


void http_task::unmap() {
	if (_M_fileaddr) {
		munmap(_M_fileaddr, _M_file_stat.st_size);
		_M_fileaddr = nullptr;
	}
}


bool http_task::write() {
	ssize_t nwrite;
	if (_M_bytes_to_send <= 0) {
		modfd(_M_epollfd, _M_sockfd, EPOLLIN);
		init();
		return true;
	}

	while (_M_bytes_to_send > 0) {
		if ((nwrite = writev(_M_sockfd, _M_iov, _M_iov_cnt)) == -1) {
			if (errno == EAGAIN) {
				modfd(_M_epollfd, _M_sockfd, EPOLLOUT);
				return true;
			}
			unmap();
			return false;
		}

		_M_bytes_have_send += nwrite;
		_M_bytes_to_send -= nwrite;
		if (_M_bytes_have_send >= _M_iov[0].iov_len) {
			_M_iov[0].iov_len = 0;
			_M_iov[1].iov_base = _M_fileaddr + (_M_bytes_have_send - _M_wridx);
			_M_iov[1].iov_len = _M_bytes_to_send;
		}
		else {
			_M_iov[0].iov_base = _M_writebuf + _M_bytes_have_send;
			_M_iov[0].iov_len = _M_iov[0].iov_len - _M_bytes_have_send;
		}
	}
	unmap();
	modfd(_M_epollfd, _M_sockfd, EPOLLIN);
	return _M_linger ? (init(), true) : false;
}


bool http_task::add_response(const char* fmt, ...) {
	if (_M_wridx >= WRITE_BUFSIZE)
		return false;
	va_list arg_list;
	va_start(arg_list, fmt);
	int len = vsnprintf(_M_writebuf + _M_wridx,
		WRITE_BUFSIZE - 1 - _M_wridx, fmt, arg_list);
	if (len >= (WRITE_BUFSIZE - 1 - _M_wridx)) {
		va_end(arg_list);
		return false;
	}
	_M_wridx += len;
	va_end(arg_list);

	LOG_INFO("request:%s", _M_writebuf);
	return true;
}


bool http_task::add_status_line(int status, const char* title) {
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}


bool http_task::add_headers(int content_len) {
	return add_content_len(content_len) && add_linger()
		&& add_blank_line();
}


bool http_task::add_content_len(int content_len) {
	return add_response("Content-Length:%d\r\n", content_len);
}


bool http_task::add_content_type(const char* type) {
	return add_response("Content-Type:%s\r\n", type);
}


bool http_task::add_linger() {
	return add_response("Connection:%s\r\n",
		_M_linger ? "keep-alive" : "close");
}


bool http_task::add_blank_line() {
	return add_response("%s", "\r\n");
}


bool http_task::add_content(const char* content) {
	return add_response("%s", content);
}


bool http_task::process_write(HTTP_CODE ret) {
	switch (ret) {
	case INTERNAL_ERROR: {
		add_status_line(500, error_500_title);
		add_headers(strlen(error_500_form));
		if (!add_content(error_500_form))
			return false;
		break;
	}
	case BAD_REQUEST: {
		add_status_line(404, error_403_title);
		add_headers(strlen(error_404_form));
		if (!add_content(error_404_form))
			return false;
		break;
	}
	case FORBIDDEN_REQUEST: {
		add_status_line(403, error_403_title);
		add_headers(strlen(error_403_form));
		if (!add_content(error_403_form))
			return false;
		break;
	}
	case FILE_REQUEST: {
		add_status_line(200, ok_200_title);
		//add_content_type("text/html");
		if (_M_file_stat.st_size != 0) {
			add_headers(_M_file_stat.st_size);
			_M_iov[0].iov_base = _M_writebuf;
			_M_iov[0].iov_len = _M_wridx;
			_M_iov[1].iov_base = _M_fileaddr;
			_M_iov[1].iov_len = _M_file_stat.st_size;
			_M_iov_cnt = 2;
			_M_bytes_to_send = _M_wridx + _M_file_stat.st_size;
			return true;
		}
		else {
			const char* ok_string = "<html><body></body></html>";
			add_headers(strlen(ok_string));
			if (!add_content(ok_string))
				return false;
		}
		break;
	}
	default:
		return false;
	}

	_M_iov[0].iov_base = _M_writebuf;
	_M_iov[0].iov_len = _M_wridx;
	_M_iov_cnt = 1;
	_M_bytes_to_send = _M_wridx;
	return true;
}


void http_task::process() {
	HTTP_CODE read_ret = process_read();
	if (read_ret == NO_REQUEST) {
		modfd(_M_epollfd, _M_sockfd, EPOLLIN);
		return;
	}
	if (process_write(read_ret))
		modfd(_M_epollfd, _M_sockfd, EPOLLOUT);
	else close_conn();
}
