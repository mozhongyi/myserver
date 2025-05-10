/*************************************************************************
  > File Name: threadpool.h
  > Author: sheep
  > Created Time: 2025年04月29日 星期二 19时32分25秒
 ************************************************************************/
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
	public:
		/*thread_number为线程池的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
		threadpool(int actor_model, connection_pool *connPool,int thread_number = 8, int max_request = 10000);
		~threadpool();

		bool append(T *request, int state);
		bool append_p(T *request);

	private:
		/*工作线程运行的函数，它不断从工作队列中取出任务并执行*/
		static void *worker(void *arg);
		void run();

	private:
		int m_thread_number;			//线程池中的线程数
		int m_max_requests;				//请求队列中允许的最大请求数
		pthread_t *m_threads;			//描述线程池的数组，其大小为m_thread_number
		std::list<T *> m_workqueue;		//请求队列
		locker m_queuelocker;			//保护请求队列的互斥锁
		sem m_queuestat;				//是否有任务需要处理
		connection_pool *m_connPool;	//数据库
		int m_actor_model;				//模型切换
};

template<typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool,int thread_number, int max_requests): m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests),m_threads(NULL),m_connPool(connPool)
{
	//检查合法输入
	if(thread_number <= 0 || max_requests <= 0)
		throw std::exception();

	//动态分配线程ID数组
	m_threads = new pthread_t[m_thread_number];
	if(!m_threads)
		throw std::exception();

	for(int i = 0; i < thread_number; ++i)
	{
		//创建线程，成功会执行worker
		if(pthread_create(m_threads + i, NULL, worker, this) != 0)
		{
			delete[] m_threads;
			throw std::exception();
		}
		//设置线程为分离状态，自动回收资源,失败直接释放内存
		if(pthread_detach(m_threads[i]))
		{
			delete[] m_threads;
			throw std::exception();
		}
	}
}

template <typename T>
threadpool<T>::~threadpool()
{
	delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *request, int state)
{
	m_queuelocker.lock();
	// 检查当前任务队列是否已满
	if(m_workqueue.size() >= m_max_requests)
	{
		m_queuelocker.unlock();
		return false;
	}

	// 设置任务状态
	request->m_state = state;
	// 将任务添加到工作队列尾部
	m_workqueue.push_back(request);
	m_queuelock.unlock();
	// 通过信号量通知工作线程有新任务到达
	m_queuestat.post();
	return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
	m_queuelocker.lock();
	if(m_workqueue.size() >= m_max_requests)
	{
		m_queuelocker.unlock();
		return false;
	}

	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();
	return true;
}

template <typename T>
//线程池工作线程的静态入口函数
void *threadpool<T>::worker(void *arg)
{
	//将void*参数转换回线程池指针
	threadpool *pool = (threadpool *)arg;
	//调用线程池的run()方法执行实际任务处理循环
	pool->run();
	//返回线程池指针(通常不被使用)
	return pool;
}

//线程池工作线程的核心执行函数，持续从任务队列获取任务并处理
template <typename T>
void threadpool<T>::run()
{
	// 无限循环，工作线程持续处理任务
	while(true)
	{
		//等待任务信号 - 阻塞直到有新任务到达
		m_queuestat.wait();
		m_queuelocker.lock();
		// 检查队列是否为空(虚假唤醒防护)
		if(m_workqueue.empty())
		{
			m_queuelocker.unlock();
			continue;
		}
		
		// 从队列获取任务
		T *request = m_workqueue.front();
		m_workqueue.pop_front();
		// 空任务检查
		if(!request)
			continue;
		if(1 == m_actor_model)
		{
			if(0 == request->m_state)		//读任务
			{
				if(request->read_once())
				{
					request->improv = 1;	// 标记处理成功
					// 从连接池获取MySQL连接(RAII方式自动管理)
					connectionRAII mysqlcon(&request->mysql, m_connPool);
					//处理请求
					request->process();
				}
				else
				{
					request->improv = 1;
					request->timer_flag = 1;	//标记超时
				}
			}
			else		//写任务
			{
				if(request->write())	//执行写操作
				{
					request->improv = 1;
				}
				else					//写失败
				{
					request->improv = 1;
					request->timer_flag = 1;
				}
			}
		}
		else	//Proactor模式
		{
			// 直接从连接池获取MySQL连接并处理
			connectionRAII mysqlcon(&request->mysql, m_connPool);
			request->process();
		}
	}
}

#endif
