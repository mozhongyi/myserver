/*************************************************************************
    > File Name: webserver.cpp
    > Author: sheep
    > Created Time: 2025年05月06日 星期二 16时03分23秒
 ************************************************************************/
#include "webserver.h"

WebServer::webServer()
{
	//http_conn类对象
	users = new http_conn[MAX_FD];

	//root文件夹路径
	char server_path[200];
	getcwd(server_path, 200);
	char root[6] = "/root"l
	m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
	strcpy(m_root, server_path);
	strcat(m_root, root);

	//定时器
	users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
	close(m_epollfd);
	close(m_listenfd);
	close(m_pipefd[1]);
	close(m_pipefd[0]);
	delete[] users;
	delete[] users_timer;
	delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,int opt_linger, int trigmode, int sql_num, int close_log, int actor_model)
{
	m_port = port;
	m_user = user;
	m_passWord = passWord;
	m_databaseName = databaseName;
	m_sql_num = sql_num;
	m_thread_num = thread_num;
	m_log_write = log_write;
	m_OPT_LINGER = opt_linger;
	m_TRIGMode = trigmode;
	m_close_log = close_log;
	m_actormodel = actor_model;
}

void WebServer::tri_mode()
{
	//LT + LT
	if(0 == m_TRIGMode)
	{
		m_LISTENTrigmode = 0;
		m_CONNTrigmode = 0;
	}
	//LT + ET
	else if(1 == m_TRIGMode)
	{
		m_LISTENTrigmode = 0;
		m_CONNTrigmode = 1;
	}
	//ET + LT
	else if(2 == m_TRIGMode)
	{
		m_LISTENTrigmode = 1;
		m_CONNTrigmode = 0;
	}
	//ET + ET
	else if(3 == TRIGMode)
	{
		m_LISTENTrigmode = 1;
		m_CONNTrigmode = 1;
	}
}

void WebServer::log_write()
{
	if(0 == m_close_log)
	{
		//初始化日志
		if(1 == m_log_write)
			Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
		else
			Log::get_instance()->init("./ServerLog",m_close_log, 2000, 800000, 0);
	}
}

void WebServer::sql_pool()
{
	//初始化数据库连接池
	m_connPool = connection_pool::GetInstance();
	m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

	//初始化数据库读取表
	users->initmysql_result(m_connPool);
}
