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
	// 当前已使用的连接数
	m_CurConn = 0;
	// 当前空闲的连接数
	m_FreeConn = 0;
}

//唯一连接池实例
connection_pool *connection_pool::GetInstance()
{
	// 静态局部变量，确保线程安全初始化
	static connection_pool connPool;
	return &connPool;
}

//初始化,url:主机地址,User:登陆数据库用户名,PassWord:密码,DBName:数据库名
//Port:数据库端口号,close_log:日志开关
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	// 保存数据库连接信息
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	// 创建MaxConn个数据库连接
	for(int i=0; i<MaxConn; i++)
	{
		MYSQL *con = NULL;
		// 初始化MYSQL对象
		con = mysql_init(con);			//初始化MYSQL对象
		if(con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		// 通过指定的数据库信息连接到数据库
		con = mysql_real_connect(con, url.c_str(), User.c_str(),PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		
		if(con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		// 将连接加入连接池列表
		connList.push_back(con);
		// 空闲连接数增加
		++m_FreeConn;
	}
	
	// 初始化信号量，初始值为空闲连接数
	reserve = sem(m_FreeConn);
	// 设置最大连接数
	m_MaxConn = m_FreeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if(0 == connList.size())
		return NULL;
	
	// 等待信号量（如果有可用连接才会继续）
	reserve.wait();

	// 对连接池操作前，先上锁
	lock.lock();
	// 取出第一个MYSQL实例
	con = connList.front();
	connList.pop_front();	

	// 当前空闲的连接池减1
	--m_FreeConn;
	// 当前已使用的连接池加1
	++m_CurConn;

	// 解锁
	lock.unlock();
	return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if(NULL == con)
		return false;
	// 加锁保证线程安全
	lock.lock();
	
	// 将连接放回连接池
	connList.push_back(con);
	// 空闲连接数加1
	++m_FreeConn;
	// 使用中连接数减1
	--m_CurConn;
	// 解锁
	lock.unlock();
	// 信号量增加1，表示有可用连接
	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{
	lock.lock();
	if(connList.size() > 0)
	{
		// 遍历所有连接并关闭
		list<MYSQL *>::iterator it;
		for(it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			// 关闭MySQL连接
			mysql_close(con);
		}
		// 重置连接计数
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}
	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

// 析构函数，自动销毁连接池
connection_pool::~connection_pool()
{
	DestroyPool();
}

// RAII类构造函数：自动获取连接
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	// 从连接池获取一个连接
	*SQL = connPool->GetConnection();

	conRAII = *SQL;
	poolRAII = connPool;
}

// RAII类析构函数：自动释放连接
connectionRAII::~connectionRAII()
{
	// 将连接归还给连接池
	poolRAII->ReleaseConnection(conRAII);
}
