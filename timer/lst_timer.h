/*************************************************************************
    > File Name: lst_timer.h
    > Author: zsy
    > Created Time: 2025年05月04日 星期日 16时18分40秒
 ************************************************************************/
#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

class util_timer;

// 客户端连接数据结构
struct client_data
{
	// 客户端地址信息
	sockaddr_in address;
	// 客户端socket文件描述符
	int sockfd;
	// 指向关联的定时器
	util_timer *timer;
};

// 定时器类
class util_timer
{
public:
	// 初始化前后指针为NULL
	util_timer() : prev(NULL), next(NULL) {}

public:
	// 定时器到期时间(绝对时间)
	time_t expire;
	
	// 回调函数指针
	void (* cb_func)(client_data *);
	// 用户数据(通常是client_data)
	client_data *user_data;
	// 前驱指针
	util_timer *prev;
	// 后继指针
	util_timer *next;
};

// 定时器链表管理类
class sort_timer_lst
{
public:
	sort_timer_lst();
	// 析构函数(释放所有定时器)
	~sort_timer_lst();

	// 添加定时器到链表(自动按过期时间排序)
	void add_timer(util_timer *timer);
	// 调整定时器位置(当定时器时间延长时调用)
	void adjust_timer(util_timer *timer);
	// 从链表中删除定时器
	void del_timer(util_timer *timer);
	// 检查并处理所有已超时的定时器
	void tick();

private:
	// 内部使用的辅助函数，将timer添加到lst_head之后的合适位置
	void add_timer(util_timer *timer, util_timer *lst_head);
	
	// 链表头指针
	util_timer *head;
	// 链表尾指针
	util_timer *tail;
};

// 工具类，整合定时器与网络操作
class Utils
{
public:
	Utils() {}
	~Utils() {}

	// 初始化时间间隔
	void init(int timeslot);

	// 对文件描述符设置非阻塞
	int setnonblocking(int fd);

	// 向epoll实例添加文件描述符
	// 将内核事件表注册读事件,ET模式，选择开启EPOLLONESHOT
	void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
	
	// 信号处理函数
	static void sig_handler(int sig);
	
	// 设置信号函数
	void addsig(int sig, void(handler)(int), bool restart = true);

	// 定时处理任务
	void timer_handler();
	
	// 显示错误信息
	void show_error(int connfd, const char *info);

public:
	// 信号通知的管道描述符（通常[0]读端，[1]写端）
	static int *u_pipefd;
	// 定时器链表（管理所有定时任务）
	sort_timer_lst m_timer_lst;
	// 全局epoll实例的文件描述符
	static int u_epollfd;
	// 定时触发间隔（单位：秒）
	int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
