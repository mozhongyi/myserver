/*************************************************************************
    > File Name: lst_timer.cpp
    > Author: sheep
    > Created Time: 2025年05月09日 星期五 16时27分08秒
 ************************************************************************/
#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
	head = NULL;
	tail = NULL;
}

//删除定时器链表的所有数据
sort_timer_lst::~sort_timer_lst()
{
	util_timer *tmp = head;
	while(tmp)
	{
		head = tmp->next;
		delete tmp;
		tmp = head;
	}
}

void sort_timer_lst::add_timer(util_timer *timer)
{
	if(!timer)
	{
		return;
	}
	//头结点为空，将插入节点设置为头结点
	if(!head)
	{
		head = tail = timer;
		return;
	}
	
	//如果定时器该定时器到期事件小于头结点的到期事件，则直接头插
	if(timer->expire < head->expire)
	{
		timer->next = head;
		head->prev = timer;
		head = timer;
		return;
	}
	
	//新结点插入到定时器链表正确地方
	add_timer(timer,head);
}

//当timer被修改后，动态调整定时器链表
void sort_timer_lst::adjust_timer(util_timer *timer)
{
	//空定时器,直接返回
	if(!timer)
	{
		return;
	}
	// 检查是否需要调整：
    // 1. 如果timer没有后继节点（已经是尾节点）
    // 2. timer的新expire仍小于后继节点的expire（依然有序）
	util_timer *tmp = timer->next;
	if(!tmp || (timer->expire < tmp->expire))
	{
		return;
	}

	//timer是头节点的情况
	if(timer == head)
	{
		head = head->next;
		head->prev = NULL;
		timer->next = NULL;
		add_timer(timer,head);
	}
	//不是头结点，先摘下该结点，在重新插入
	else
	{
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		add_timer(timer, timer->next);
	}
}

void sort_timer_lst::del_timer(util_timer *timer)
{
	//该结点为空结点
	if(!timer)
	{
		return;
	}

	//链表中只有一个结点，且为该结点
	if((timer == head) && (timer == tail))
	{
		delete timer;
		head = NULL;
		tail = NULL;
		return;
	}

	//该结点为尾结点
	if(timer == tail)
	{
		tail = tail->prev;
		tail->next = NULL;
		delete timer;
		return;
	}

	//该结点为中间结点
	timer->prev->next = timer->next;
	timer->next->prev = timer->prev;
	delete timer;
}

//检查并执行所有到期（expire <= 当前时间）的定时器回调函数
void sort_timer_lst::tick()
{
	if(!head)
	{
		return;
	}

	//获取当前系统的时间
	time_t cur = time(NULL);
	util_timer *tmp =head;
	while(tmp)
	{
		//发现未到期的定时器，直接终止
		if(cur < tmp->expire)
		{
			break;
		}

		//执行回调函数
		tmp->cb_func(tmp->user_data);
		//删除已经处理后的定时器
		head = tmp->next;
		if(head)
		{
			head->prev = NULL;
		}
		delete tmp;
		tmp = head;
	}
}

//辅助函数：用于将定时器任务插入到链表正确位置
//调用前已经确定head的位置不是正确位置
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
	util_timer *prev = lst_head;
	util_timer *tmp = prev->next;
	//循环遍历定时器链表
	while(tmp)
	{
		if(timer->expire < tmp->expire)
		{
			prev->next = timer;
			timer->next = tmp;
			tmp->prev = timer;
			timer->prev = prev;
			break;
		}

		prev = tmp;
		tmp = tmp->next;
	}
	
	//遍历到最后也没找到位置，说明插入点在链表尾部
	if(!tmp)
	{
		prev->next = timer;
		timer->prev = prev;
		timer->next = NULL;
		tail = timer;
	}
}

void Utils::init(int timeslot)
{
	m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
	//获取当前文件状态标志
	int old_option = fcntl(fd, F_GETFL);
	//添加非阻塞标志(O_NONBLOCK)，ET模式必须配合非阻塞
	int new_option = old_option | O_NONBLOCK;
	//设置新的文件状态标志
	fcntl(fd, F_SETFL, new_option);
	//返回原始设置用于恢复
	return old_option;
}

//将内核事件表注册读事件、ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
	epoll_event event;
	event.data.fd = fd;				//设置关联的文件描述符

	if(1 == TRIGMode)
		event.events = EPOLLIN | EPOLLET |EPOLLRDHUP;
	else
		event.events = EPOLLIN | EPOLLRDHUP;
	
	if(one_shot)
		event.events |= EPOLLONESHOT;

	//注册到epoll实例中
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

	//设置非阻塞模式，ET模式必须配合非阻塞
	setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
	//为保证函数的可重入性，保留原来的errno
	int save_errno = errno;
	int msg = sig;
	//将信号写入管道，由epoll模型监听处理，信号处理过程能修改errno
	send(u_pipefd[1], (char *)&msg, 1, 0);
	errno = save_errno;
}

//设置信号函数，sig为处理信号的编号，handler为信号处理函数的指针，restart表示被信号中断的系统调用是否重启
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));

	//设置信号处理函数，当sig发生时，handler将被调用
	sa.sa_handler = handler;

	//如果需要自动重启被中断的系统调用，则设置 SA_RESTART 标志
	if(restart)
	{
		sa.sa_flags |= SA_RESTART;
	}

	//阻塞所有其他信号（处理当前信号时）
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
	m_timer_lst.tick();
	alarm(m_TIMESLOT);
}

//向客户端发送错误信息并关闭连接
void Utils::show_error(int connfd, const char *info)
{
	send(connfd, info, strlen(info), 0);
	close(connfd);
}

int *Utils::u_pipefd = 0;				//管道文件描述符
int Utils::u_epollfd = 0;				//epoll实例的文件描述符

class Utils;
//关闭客户端连接并释放相关资源
void cb_func(client_data *user_data)
{
	epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
	assert(user_data);
	close(user_data->sockfd);					
	http_conn::m_user_count--;				//更新在线用户数量
}
