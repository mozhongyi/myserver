/*************************************************************************
    > File Name: webserver.cpp
    > Author: sheep
    > Created Time: 2025年05月06日 星期二 16时03分23秒
 ************************************************************************/
#include "webserver.h"

WebServer::WebServer()
{
	//http_conn类对象
	users = new http_conn[MAX_FD];

	//root文件夹路径
	char server_path[200];
	getcwd(server_path, 200);
	char root[6] = "/root";
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

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
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
	else if(3 == m_TRIGMode)
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
	address.sin_port = htons(m_port);

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
	users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

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

// 处理客户端连接请求的函数
bool WebServer::dealclientdata()
{
	// 定义客户端地址结构
	struct sockaddr_in client_address;
	socklen_t client_addrlength = sizeof(client_address);
	// 根据监听模式决定处理方式
	if(0 == m_LISTENTrigmode)		// 如果是水平触发模式(LT)
	{
		// 接受一个客户端连接
		int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
		// 接受连接失败
		if(connfd < 0)
		{
			LOG_ERROR("%s:errno is:%d", "accept error", errno);
			return false;
		}
		// 检查是否超过最大连接数限制
		if(http_conn::m_user_count >= MAX_FD)
		{
			// 向客户端发送服务器繁忙错误,并关闭连接
			utils.show_error(connfd, "Internal server busy");
			LOG_ERROR("%s", "Internal server busy");
			return false;
		}
		// 为新连接创建定时器
		timer(connfd, client_address);
	}
	// 如果是边缘触发模式(ET)
	else
	{
		// 边缘触发模式下需要循环accept直到没有新连接为止
		while(1)
		{
			int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
			// 接受连接失败
			if(connfd < 0)
			{
				LOG_ERROR("%s:errno is:%d", "accept error", errno);
				break;
			}
			// 检查是否超过最大连接数限制
			if(http_conn::m_user_count >= MAX_FD)
			{
				utils.show_error(connfd, "Internal server busy");
				LOG_ERROR("%s", "Internal server busy");
				break;
			}
			// 为新连接创建定时器
			timer(connfd, client_address);
		}
		// ET模式下总是返回false
		return false;
	}
	// LT模式下成功处理返回true
	return true;
}

// 处理信号的函数
// timeout - 输出参数，标记是否收到超时信号
// stop_server - 输出参数，标记是否收到停止服务信号
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
	int ret = 0;
	int sig;
	// 用于接收信号的缓冲区
	char signals[1024];
	// 从管道读取信号数据
	ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
	// 错误处理
	if(ret == -1)
	{
		return false;
	}
	// 管道关闭
	else if(ret == 0)
	{
		return false;
	}
	else
	{
		for(int i = 0; i < ret; ++i)
		{
			// 检查每个信号类型
			switch(signals[i])
			{
			// 定时器信号(超时信号)
			case SIGALRM:
			{
				// 设置超时标志
				timeout = true;
				break;
			}
			// 终止信号
			case SIGTERM:
			{
				// 设置停止服务标志
				stop_server = true;
				break;
			}

			}
		}
	}
	return true;
}

// 处理读事件的函数
// sockfd - 发生读事件的套接字描述符
void WebServer::dealwithread(int sockfd)
{
	// 获取与该套接字关联的定时器
	util_timer *timer = users_timer[sockfd].timer;
	
	// Reactor模式处理
	if(1 == m_actormodel)	// Reactor模式
	{
		if(timer)
		{
			adjust_timer(timer);
		}

		// 若监测到读事件，将该事件放入请求队列
		// 将读事件放入线程池请求队列
        // 参数: users + sockfd - 对应的用户连接对象
        //       0 - 表示读事件
		m_pool->append(users + sockfd, 0);

		// 等待工作线程处理完成
		while(true)
		{
			// 检查improv标志，表示工作线程已完成处理
			if(1 == users[sockfd].timer_improv)
			{
				if(1 == users[sockfd].timer_flag)
				{
					deal_timer(timer, sockfd);
					users[sockfd].timer_flag = 0;
				}
				users[sockfd].improv = 0;
				break;
			}
		}
	}
	// Proactor模式
	else
	{
		// 直接读取数据(主线程完成IO操作)
		if(users[sockfd].read_once)			// 成功读取数据
		{
			LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

			// 若监测到读事件，将该事件放入请求队列
			// 将请求放入线程池处理(只处理业务逻辑)
			m_pool->append_p(users+sockfd);
			// 调整定时器
			if(timer)
			{
				adjust_timer(timer);
			}
		}
		// 读取失败
		else
		{
			// 处理定时器(关闭连接等)
			deal_timer(timer, sockfd);
		}
	}
}

// 处理客户端写操作
void WebServer::dealwithwrite(int sockfd)
{
	// 获取该socket的定时器（用于超时管理）
	util_timer *timer = users_timer[sockfd].timer;
	// Reactor 模式：I/O 操作由线程池处理
	if(1 == m_actormodel)
	{
		// 更新定时器，防止处理期间超时
		if(timer)
		{
			adjust_timer(timer);
		}
		// 提交写任务到线程池
		m_pool->append(users + sockfd, 1);
		// 等待任务完成
		while(true)
		{
			if(1 == users[sockfd].improv)
			{
				// 如果任务期间发生超时，关闭连接
				if(1 == users[sockfd].timer_flag)
				{
					// 如果任务期间发生超时，关闭连接
					deal_timer(timer, sockfd);
					// 重置超时
					users[sockfd].timer_flag = 0;
				}
				// 重置任务状态
				users[sockfd].improv = 0;
				break;
			}
		}
	}
	// Proactor 模式：直接尝试写（通常由系统异步完成）
	else
	{
		// 尝试发送数据
		if(users[sockfd].write())
		{
			LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
			// 更新定时器，防止超时
			if(timer)
			{
				adjust_timer(timer);
			}
		}
		// 写失败(如连接断开)，关闭socket
		else
		{
			deal_timer(timer, sockfd);
		}
	}
}

// 基于epoll的事件循环，服务器的核心部分
void WebServer::eventLoop()
{
	// 标记是否发生超时事件
	bool timeout = false;
	// 标记是否停止服务器
	bool stop_server = false;

	while(!stop_server)
	{
		// 等待事件发生，无限等待(-1表示阻塞直到有事件发生)
		int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
		// 处理epoll_wait错误
		if(number < 0 && errno != EINTR)
		{
			LOG_ERROR("%s", "epoll failure");
			break;
		}
		
		// 处理所有就绪的事件
		for(int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd;

			//处理新到的客户连接
			if(sockfd == m_listenfd)
			{
				// 处理新客户端连接
				bool flag = dealclientdata();
				if(false == flag)
					// 处理失败则跳过
					continue;
			}
			// 处理连接关闭或错误事件
			else if(events[i].event & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
			{
				// 服务器端关闭连接，移除对应的定时器
				util_timer *timer = users_timer[sockfd].timer;
				deal_timer(timer, sockfd);
			}
			// 处理信号事件(通过管道传递的信号)
			else if((sockfd == m_pipefd[0] && (events[i].events & EPOLLIN)))
			{
				bool flag = dealwithsignal(timeout, stop_server);
				if(false == flag)
					LOG_ERROR("%s", "dealclientdata failure");
			}
			// 处理客户连接上接受到的数据
			else if(events[i].events & EPOLLIN)
			{
				dealwithread(sockfd);
			}
			// 处理可写事件
			else if(events[i].events & EPOLLOUT)
			{
				dealwithwrite(sockfd);
			}
		}
		// 如果发生了超时事件，处理定时器
		if(timeout)
		{
			// 执行定时器处理函数
			utils.timer_handler();
			LOG_INFO("%s", "timer tick");

			timeout = false;
		}
	}
}
