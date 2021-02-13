#ifndef HTTP_TASK_H_
#define HTTP_TASK_H_

#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mysql/mysql.h>
#include <unordered_map>
#include <string>

#include "../mysql/sql_connpool.h"

class http_task {
public:
	static constexpr int FILENAME_LEN = 256;
	static constexpr int READ_BUFSIZE = 2048;
	static constexpr int WRITE_BUFSIZE = 1024;
	enum METHOD {
		GET, POST, HEAD, PUT, DELETE, TRACE,
		OPTIONS, CONNECT, PATH
	};
	enum CHECK_STATE {
		CHECK_STATE_REQUESTLINE,
		CHECK_STATE_HEADER,
		CHECK_STATE_CONTENT
	};
	enum HTTP_CODE {
		NO_REQUEST, GET_REQUEST,
		BAD_REQUEST, NO_RESOURCE,
		FORBIDDEN_REQUEST, FILE_REQUEST,
		INTERNAL_ERROR, CLOSED_CONNECTION
	};
	enum LINE_STATUS {
		LINE_OK, LINE_BAD, LINE_OPEN
	};

public:
	http_task() = default;
	~http_task() = default;

public:
	void init(int sockfd, const struct sockaddr_in& cliaddr,
		char* root, int closelog, const std::string& user,
		const std::string& passwd, const std::string& sqlname);
	const struct sockaddr_in* getcliaddr() const { return &_M_cliaddr; }
	void initmysql_result(sql_connpool* connpool);
	void close_conn(bool real_close = true);
	void process();
	bool read_once();
	bool write();

private:
	void init();
	LINE_STATUS parse_line();
	char* get_line() { return _M_readbuf + _M_start_line; }
	HTTP_CODE process_read();
	bool process_write(HTTP_CODE ret);
	HTTP_CODE parse_request_line(char* text);
	HTTP_CODE parse_header(char* text);
	HTTP_CODE parse_content(char* text);
	HTTP_CODE do_request();

	bool add_response(const char* fmt, ...);
	bool add_status_line(int status, const char* title);
	bool add_content_type(const char* type);
	bool add_headers(int content_len);
	bool add_content_len(int content_len);
	bool add_linger();
	bool add_blank_line();
	bool add_content(const char* content);
	void unmap();

public:
	static int _M_epollfd;
	static int _M_user_cnt;
	MYSQL* _M_mysql;

private:
	/* 用户信息 */
	struct sockaddr_in _M_cliaddr;
	int _M_sockfd;

	/* 读缓冲区 */
	char _M_readbuf[READ_BUFSIZE];
	int _M_rdidx;
	int _M_chkidx;
	int _M_start_line;

	/* 写缓冲区 */
	char _M_writebuf[WRITE_BUFSIZE];
	int _M_wridx;

	CHECK_STATE _M_check_state;
	METHOD _M_method;

	/* HTTP报文解析结果 */
	char _M_real_file[FILENAME_LEN];
	char* _M_url;
	char* _M_version;
	char* _M_host;
	int _M_content_len;
	bool _M_linger;	//是否启动长连接
	char* _M_string;

	//* 请求文件信息 */
	void* _M_fileaddr;
	struct stat _M_file_stat;
	struct iovec _M_iov[2];
	int _M_iov_cnt;
	int _M_bytes_to_send;
	int _M_bytes_have_send;
	char* _M_root;

	/* 其他 */
	int _M_cgi;		//开启POST请求actor动作码分析
	int _M_close_log;
	char _M_sql_user[128];
	char _M_sql_passwd[128];
	char _M_sql_name[128];
};


#endif // !HTTP_TASK_H_
