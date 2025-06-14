/*************************************************************************
    > File Name: sql_connection_pool.h
    > Author: sheep
    > Created Time: 2025年05月04日 星期日 15时55分34秒
 ************************************************************************/
#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
	MYSQL *GetConnection();					// 获取数据库连接
	bool ReleaseConnection(MYSQL *conn);	// 释放连接
	int GetFreeConn();						// 获取当前空闲连接数
	void DestroyPool();						// 销毁所有连接

	//单例模式
	static connection_pool *GetInstance();

	/**
     * 初始化连接池
     * @param url 数据库主机地址
     * @param User 数据库用户名
     * @param PassWord 数据库密码
     * @param DataBaseName 数据库名
     * @param Port 数据库端口
     * @param MaxConn 最大连接数
     * @param close_log 日志开关
     */
	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;							// 最大连接数
	int m_CurConn;							// 当前已使用的连接数
	int m_FreeConn;							// 当前空闲的连接数
	locker lock;
	list<MYSQL *> connList;					// 连接池
	sem reserve;

public:
	string m_url;							// 主机地址
	string m_Port;							// 数据库端口号
	string m_User;							// 登陆数据库用户名
	string m_PassWord;						// 登陆数据库密码
	string m_DatabaseName;					// 使用数据库名
	int m_close_log;						// 日志开关
};

class connectionRAII
{
public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();

private:
	// 持有的数据库连接
	MYSQL *conRAII;
	// 所属连接池
	connection_pool *poolRAII;
};

#endif
