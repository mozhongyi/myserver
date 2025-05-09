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

//初始化线程池
void WebServer::thread_pool()
{
	//线程池初始化
	m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

//
void WebServer::eventListen()
{
	//网络编程基础步骤
	m_listenfd = socket(PF_INET, SOCK_STREAM, 0);		
	assert(m_listenfd >= 0);			//确保套接字创建成功，否则终止程序

	//优雅关闭连接
	if(0 == m_OPT_LINGER)		//非优雅关闭，调用close直接关闭
	{
		struct linger tmp = {0,1};
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}
	else if(1 == m_OPT_LINGER)	//优雅关闭，调用close直到：1s后，发送完毕
	{
		struct linger tmp = {1,1};
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}

	int ret = 0;
	struct sockaddr_in address;
	//将address内存清零，避免脏数据
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htos(m_port);

	int flag = 1;
	/*
	 * SO_REUSEADDR 选项作用：
 	 * 1. 允许立即重用处于TIME_WAIT状态的端口
 	 * 2. 允许多个套接字绑定到相同IP和端口(在多播/广播时有用)
     * 3. 服务器崩溃后可以快速重启而不需要等待系统释放端口
 	 */
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
	assert(ret >= 0);
	ret = listen(m_listenfd, 5);
	assert(ret >= 0);

	utils.init(TIMESLOT);

	//epoll创建内核事件表
	epoll_event events[MAX_EVENT_NUMBER];
	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);

	utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
	http_conn::m_epollfd = m_epollfd;
}
