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

	//创建一对相互连接的UNIX域socket，用于进程间通信，实现m_pipefd通信
	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
	assert(ret != -1);
	utils.setnonblocking(m_pipefd[1]);
	//将管道读端添加到epoll中监听
	utils.addfd(m_epollfd, m_pipefd[0], false, 0);
	
	//丢弃SIGPiPE信号，防止程序意外终止
	utils.addsig(SIGPIPE, SIG_IGN);
	utils.addsig(SIGALRM, utils.sig_handler, false);
	utils.addsig(SIGTERM, utils.sig_handler, false);

	alarm(TIMESLOT);

	//保存管道和epoll文件描述符到工具类中
	Utils::u_pipefd = m_pipefd;
	Utils::u_epollfd = m_epollfd;
}

//初始化用户连接数据,创建并设置定时器,将定时器添加到定时器链表
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
	users[connfd].init(connfd, client_address, m_root, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

	//初始化client_data数据
	users_timer[connfd].address = client_address;
	users_timer[connfd].sockfd = connfd;
	//创建新的定时器对象
	util_timer *timer = new util_timer;
	timer->user_data = &users_timer[connfd];
	timer->cb_func = cb_func;
	//设置定时器过期时间
	time_t cur = time(NULL);
	timer->expire = cur + 3 * TIMESLOT;
	//将定时器与客户端关联
	users_timer[connfd].timer = timer;
	//将定时器添加到定时器链表进行统一管理
	utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
	time_t cur = time(NULL);
	timer->expire = cur + 3 * TIMESLOT;
	utils.m_timer_lst.adjust_timer(timer);

	LOG_INFO("%s", "adjust timer once");
}

//处理定时器到期事件的函数，主要完成定时器回调触发、资源清理和日志记录工作
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
	// 触发回调，处理连接关闭等操作
	timer->cb_func(&users_timer[sockfd]);
	if(timer)
	{
		// 从定时器链表中移除该定时器
		utils.m_timer_lst.del_timer(timer);
	}
	//获取socket fd并记录日志
	LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}


