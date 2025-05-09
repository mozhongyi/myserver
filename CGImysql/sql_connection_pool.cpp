/*************************************************************************
    > File Name: sql_connection_pool.cpp
    > Author: sheep
    > Created Time: 2025年05月08日 星期四 16时09分24秒
 ************************************************************************/
#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

//当前已使用的连接数和当前空闲的连接数都设置为0
connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

//唯一连接池实例
connection_pool *conection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//初始化,url:主机地址,User:登陆数据库用户名,PassWord:密码,DBName:数据库名
//Port:数据库端口号,close_log:日志开关
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int close_log)
{
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DataBaseName = DBName;
	m_close_log = close_log;

	for(int i=0; i<MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);			//初始化MYSQL对象
		if(con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		//通过指定的数据库信息连接到数据库
		con = mysql_real_connect(con, url.c_str(), User.c_str(),PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if(con == NULL)
		{
			LOG_error("MySQL Error");
			exit(1);
		}
		connList.push_back(con);
		++m_FreeConn;				//当前空闲的连接数加1
	}

	reserve = sem(m_FreeConn);

	m_MaxConn = m_FreeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if(0 == connList.size())
		return NULL;
	
	reserve.wait();

	lock.lock();				//对连接池操作前，先上锁
	con = connList.front();		//取出第一个MYSQL实例	
	connList.pop_front();		

	--m_FreeConn;				//当前空闲的连接池减1
	++m_CurConn;				//当前已使用的连接池加1

	lock.unlock();				//解锁
	return con;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{
	lock.lock();
	if(connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for(it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connection(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->GetConnection();

	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
	poolRAII->ReleaseConnection(connRAII);
}
