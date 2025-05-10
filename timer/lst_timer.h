/*************************************************************************
    > File Name: lst_timer.h
    > Author: zsy
    > Created Time: 2025年05月04日 星期日 16时18分40秒
 ************************************************************************/
#ifndef LST_TIMER
#define LSR_TIMER

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
#include <../log/log.h>

class util_timer;

struct client_data
{
	sockaddr_in address;
	int sockfd;
	util_timer *timer;
};

class util_timer
{
public:
	util_timer() : prev(NULL), next(NULL) {}

public:
	time_t expire;			//定时器到期时间

	void (* cb_func)(client_data *);
	client_data *user_data;
	util_timer *prev;
	util_timer *next;
};

class sort_timer_lst
{
public:
	sort_timer_lst();
	~sort_timer_lst();

	void add_timer(util_timer *timer);
	void adjust_timer(util_timer *timer);
	void del_timer(util_timer *timer);
	void tick();

private:
	void add_timer(util_timer *timer, util_timer *lst_head);

	util_timer *head;
	util_timer *tail;
};

class Utils
{
public:
	Utils() {}
	~Utils() {}

	void init(int timeslot);

	//对文件描述符设置非阻塞
	int setnonblocking(int fd);

	//将内核事件表注册读事件,ET模式，选择开启EPOLLONESHOT
	void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
	
	//信号处理函数
	static void sig_handler(int sig);
	
	//设置信号函数
	void addsig(int sig, void(handler)(int), bool restart = true);

	//定时处理任务
	void timer_hander();
	
	void show_error(int connfd, const char *info);

public:
	static int *u_pipefd;	//信号通知的管道描述符（通常[0]读端，[1]写端）
	sort_timer_lst m_timer_lst;		//定时器链表（管理所有定时任务）
	static int u_epollfd;			// 全局epoll实例的文件描述符
	int m_TIMESLOT;					// 定时触发间隔（单位：秒）
};

void cb_func(client_data *user_data);

#endif
