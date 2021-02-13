#include "../mysql/sql_connpool.h"
#include "../log/log.h"


sql_connpool* sql_connpool::get_instance() {
	static sql_connpool connpool;
	return &connpool;
}


void sql_connpool::init(const std::string& url, const std::string& user,
		const std::string& passwd, const std::string& database, int port, 
		size_t maxconn, int closelog) {
	_M_url = url;
	_M_user = user;
	_M_passwd = passwd;
	_M_database = database;
	_M_port = port;

	for (size_t i = 0; i < maxconn; ++i) {
		MYSQL* conn = nullptr;

		if ((conn = mysql_init(nullptr)) == nullptr) {
			LOG_ERROR("mysql_init error");
			exit(EXIT_FAILURE);
		}
		if ((conn = mysql_real_connect(conn, url.c_str(), user.c_str(),
			passwd.c_str(), database.c_str(), port, nullptr, 0)) == nullptr) {
			LOG_ERROR("mysql_real_connect error");
			exit(EXIT_FAILURE);
		}
		std::cout << "create a connection to Mysql" << std::endl;
		_M_connlist.push_back(conn);
		++_M_freeconn;
	}
	_M_maxconn = _M_freeconn;
}


/* 从Mysql连接池链表取出一个连接 */
MYSQL* sql_connpool::get_conn() {
	MYSQL* conn = nullptr;
	_M_locker.lock();
	while (_M_freeconn <= 0)
		_M_cond.wait(_M_locker.get());
	conn = _M_connlist.front();
	_M_connlist.pop_front();
	--_M_freeconn;
	++_M_curconn;
	_M_locker.unlock();
	return conn;
}


/* 返还一个Mysql连接到连接池链表中 */
bool sql_connpool::release_conn(MYSQL* conn) {
	if (!conn) return false;
	_M_locker.lock();
	_M_connlist.push_back(conn);
	++_M_freeconn;
	--_M_curconn;
	_M_locker.unlock();
	_M_cond.signal();
	return true;
}


/* 释放Mysql连接池中的所有连接 */
void sql_connpool::destroy_pool() {
	_M_locker.lock();
	if (!_M_connlist.empty()) {
		for (auto& elem : _M_connlist)
			mysql_close(elem);
		_M_curconn = 0;
		_M_freeconn = 0;
		_M_connlist.clear();
	}
	_M_locker.unlock();
}
