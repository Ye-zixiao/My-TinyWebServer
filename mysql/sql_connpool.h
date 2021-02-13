#ifndef SQL_CONNECTION_POOL_H_
#define SQL_CONNECTION_POOL_H_

#include <mysql/mysql.h>
#include "../synchronize/synchronize.h"
#include <string>
#include <list>

/* mysql连接池 */
class sql_connpool {
public:
	~sql_connpool() { destroy_pool(); }

	static sql_connpool* get_instance();

	void init(const std::string& url, const std::string& user, const std::string& passwd,
		const std::string& database, int port, size_t maxconn, int closelog = true);
	MYSQL* get_conn();
	bool release_conn(MYSQL* conn);
	void destroy_pool();
	int freeconn_size() const { return _M_freeconn; }

private:
	sql_connpool() :_M_curconn(0), _M_freeconn(0) {}

private:
	std::list<MYSQL*> _M_connlist;
	size_t _M_maxconn;
	size_t _M_curconn;
	size_t _M_freeconn;
	Locker _M_locker;
	Cond _M_cond;

public:
	int _M_port;
	std::string _M_url;
	std::string _M_user;
	std::string _M_passwd;
	std::string _M_database;
	std::string _M_closelog;
};

/* 获取连接的RAII类，以使得MySQL连接在初始时自动获取连接，
	在析构时自动向连接池归还连接 */
class sqlconnRAII {
public:
	sqlconnRAII(MYSQL*& conn, sql_connpool* connpool) :
		_M_connpool(connpool), _M_sqlconn(connpool->get_conn()) {
		conn = _M_sqlconn;
	}
	~sqlconnRAII() { _M_connpool->release_conn(_M_sqlconn); }

private:
	sql_connpool* _M_connpool;
	MYSQL* _M_sqlconn;
};


#endif // !SQL_CONNECTION_POOL_H_
