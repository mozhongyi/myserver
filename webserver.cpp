#include "webserver.h"

WebServer::WebServer()
{
    // 创建HTTP连接对象数组（每个文件描述符对应一个连接）
    users = new http_conn[MAX_FD];

    // 获取服务器根目录路径
    char server_path[200];
	// 获取当前工作目录
    getcwd(server_path, 200);
	// 定义根目录子路径（通常存放网页文件）
    char root[6] = "/root";
	// 分配内存保存完整根目录路径（基础路径 + 子路径）
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
	// 复制基础路径
    strcpy(m_root, server_path);
	// 拼接root目录
    strcat(m_root, root);

    // 创建客户端定时器数组，用于管理客户端连接超时
    // 每个连接对应一个定时器对象，实现非活跃连接的自动关闭
    users_timer = new client_data[MAX_FD];
}

// 释放所有资源
WebServer::~WebServer()
{
	// 关闭epoll文件描述符
    close(m_epollfd);
	// 关闭监听socket
    close(m_listenfd);
	// 关闭管道写端
    close(m_pipefd[1]);
	// 关闭管道读端
    close(m_pipefd[0]);
	// 释放HTTP连接数组
    delete[] users;
	// 释放定时器数组
    delete[] users_timer;
	// 删除线程池
    delete m_pool;
}

// 配置服务器参数
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
	// 监听端口
    m_port = port;
	// 数据库用户名
    m_user = user;
	// 数据库密码
    m_passWord = passWord;
	// 数据库名
    m_databaseName = databaseName;
    // SQL连接池大小
	m_sql_num = sql_num;
	// 线程池线程数
    m_thread_num = thread_num;
    // 日志写入方式（同步/异步）
	m_log_write = log_write;
	// 优雅关闭选项
    m_OPT_LINGER = opt_linger;
    // 触发模式组合
	m_TRIGMode = trigmode;
    // 日志开关
	m_close_log = close_log;
	// 并发模型（Reactor/Proactor）
    m_actormodel = actor_model;
}


// 根据配置设置监听和连接的触发模式
void WebServer::trig_mode()
{
	// LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
	{
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

// 根据m_close_log和m_log_write参数初始化日志系统
// 支持同步和异步两种日志写入模式
void WebServer::log_write()
{
	// 如果m_close_log为0，表示开启日志功能
    if (0 == m_close_log)
    {
        // 同步日志（阻塞队列大小=0）或异步日志（阻塞队列大小=800）
        if (1 == m_log_write)
			// 异步日志模式：使用阻塞队列实现异步写日志
            // 日志文件位于"./ServerLog"目录
            // 日志缓冲区大小2000字节，最大日志行数800000，阻塞队列大小800
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
			// 同步日志模式：直接写入文件，不使用队列
            // 队列大小为0表示同步模式
            // 其他参数与异步模式相同
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表,此处只初始化了第一个数组元素
    users->initmysql_result(m_connPool);
}

// 创建线程池（指定并发模型、连接池、线程数）
void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
	// 断言确保套接字创建成功
    assert(m_listenfd >= 0);

    // SO_LINGER选项控制TCP连接关闭行为
    if (0 == m_OPT_LINGER)
    {
		// 禁用优雅关闭：close()立即返回，可能丢失未发送数据
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
		// 启用优雅关闭：close()等待1秒让数据发送完成
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

	// 地址绑定
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

	// 设置端口复用选项，允许服务器重启时快速绑定端口
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    // 绑定地址和端口
	ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

	// 初始化定时器，时间槽大小为TIMESLOT秒
    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    // 将epoll句柄设置为HTTP连接类的共享成员
	http_conn::m_epollfd = m_epollfd;

	// 信号处理管道创建
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
	// 设置管道写端为非阻塞
    utils.setnonblocking(m_pipefd[1]);
	// 将读端添加到epoll事件表，用于接收信号事件
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);
	
	// 信号处理设置
    utils.addsig(SIGPIPE, SIG_IGN);
	// 处理定时器信号
    utils.addsig(SIGALRM, utils.sig_handler, false);
	// 处理终止信号
    utils.addsig(SIGTERM, utils.sig_handler, false);
	// 设置定时器，触发SIGALRM信号
    alarm(TIMESLOT);

    // 设置工具类的静态成员，用于信号处理函数访问
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

// 为新连接创建并初始化定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
	// 初始化HTTP连接对象
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    // 创建新的定时器对象
	util_timer *timer = new util_timer;
    // 将定时器与客户端数据关联
	timer->user_data = &users_timer[connfd];
    // 设置定时器超时回调函数
    // 当定时器超时时，将调用此函数来关闭连接并释放资源
	timer->cb_func = cb_func;
    // 计算定时器过期时间：当前时间加上3倍的TIMESLOT
	time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    // 将定时器关联到客户端数据
	users_timer[connfd].timer = timer;
    // 将定时器添加到定时器链表中进行管理
    // 升序链表，按超时时间排序
	utils.m_timer_lst.add_timer(timer);
}

// 当客户端有活动时，调用此函数延长该客户端连接的超时时间
void WebServer::adjust_timer(util_timer *timer)
{
	// 获取当前时间
    time_t cur = time(NULL);
	// 重新设置定时器的超时时间：当前时间加上3倍的TIMESLOT
    timer->expire = cur + 3 * TIMESLOT;
	// 调用定时器链表的调整函数，将定时器重新排序
    utils.m_timer_lst.adjust_timer(timer);
	// 记录日志，表示定时器已调整
    LOG_INFO("%s", "adjust timer once");
}

// 当客户端连接超时时，调用此函数执行清理操作
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
	// 执行定时器的回调函数，关闭连接和释放资源
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
		// 从定时器链表中删除该定时器
        utils.m_timer_lst.del_timer(timer);
    }
	
	// 记录日志，表示关闭了指定的文件描述符
    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

// 处理客户端连接请求
bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);   
    // 水平触发模式（LT）：默认模式
	if (0 == m_LISTENTrigmode)
    {
		// 接受一个客户端连接
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
		// 检查是否超过最大连接
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
		// 为新连接创建定时器并初始化相关资源
        timer(connfd, client_address);
    }
	// 边缘触发模式（ET）：需要一次性处理完所有连接
    else
    {
        while (1)
        {
			// 持续接受客户端连接，直到返回-1且errno为EAGAIN或EWOULDBLOCK
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
			// 检查是否超过最大连接数
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
			// 为新连接创建定时器并初始化相关资源
            timer(connfd, client_address);
        }
        return false;
    }

    return true;
}

// 处理信号管道中的信号
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    // 信号值
	int sig;
	// 存储接收到的信号值
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
		// 遍历所有接收到的信号值
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
			// 定时器信号：触发超时处理
            case SIGALRM:
            {
                timeout = true;
                break;
            }
			// 终止信号：优雅关闭服务器
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

// 处理客户端读事件
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // Reactor模式：主线程接收事件，工作线程处理读写
    if (1 == m_actormodel)
    {
		// 调整定时器，延长连接超时时间
        if (timer)
        {
            adjust_timer(timer);
        }

        // 将读任务添加到线程池请求队列
        // users + sockfd 指向对应客户端的http_conn对象
        // 0 表示这是一个读任务
        m_pool->append(users + sockfd, 0);

        while (true)
        {
			// improv标志由工作线程设置，表示处理完成
            if (1 == users[sockfd].improv)
            {
				// timer_flag表示连接需要关闭
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
	// Proactor模式：主线程负责读写，工作线程处理业务逻辑
    else
    {
		// 主线程直接读取数据
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);
			
			// 读取成功，调整定时器
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
			// 读取失败，关闭连接并删除定时器
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}
