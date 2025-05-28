/*************************************************************************
    > File Name: block_queue.h
    > Author: sheep
    > Created Time: 2025年05月05日 星期一 16时05分37秒
 ************************************************************************/
 /*
 *循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
 *线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
 */

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template <class T>
class block_queue
{
public:
	// 构造函数，初始化队列
	block_queue(int max_size = 1000)
	{
		if(max_size <= 0)
		{
			// 非法大小，直接退出
			exit(-1);
		}

		m_max_size = max_size;
		// 分配数组空间
		m_array = new T[max_size];		
		// 当前元素数量
		m_size = 0;
		// 队首索引
		m_front = -1;
		// 队尾索引
		m_back = -1;
	}

	// 清空队列（注意：并不释放内存）
	void clear()
	{
		m_mutex.lock();
		m_size = 0;
		m_front = -1;
		m_back = -1;
		m_mutex.unlock();
	}
	
	// 析构函数，释放内存
	~block_queue()
	{
		m_mutex.lock();
		if(m_array != NULL)
			delete []m_array;

		m_mutex.unlock();
	}

	//判断队列是否满了
	bool full()
	{
		m_mutex.lock();
		if(m_size >= m_max_size)
		{
			m_mutex.unlock();
			return true;
		}
		m_mutex.unlock();
		return false;
	}

	//判断队列是否为空
	bool empty()
	{
		m_mutex.lock();
		if(0 == m_size)
		{
			m_mutex.unlock();
			return true;
		}
		m_mutex.unlock();
		return false;
	}

	// 获取队首元素（不删除）
	bool front(T &value)
	{
		m_mutex.lock();
		if(0 == m_size)
		{
			m_mutex.unlock();
			return false;
		}
		value = m_array[m_front];
		m_mutex.unlock();
		return true;
	}

	// 获取队尾元素（不删除）
	bool back(T &value)
	{
		m_mutex.lock();
		if(0 == m_size)
		{
			m_mutex.unlock();
			return false;
		}
		value = m_array[m_back];
		m_mutex.unlock();
		return true;
	}

	// 获取当前队列大小
	int size()
	{
		int tmp = 0;

		m_mutex.lock();
		tmp = m_size;

		m_mutex.unlock();
		return tmp;
	}
	
	// 获取队列最大容量
	int max_size()
	{
		int tmp = 0;

		m_mutex.lock();
		tmp = m_max_size;
		
		m_mutex.unlock();
		return tmp;
	}

	//往队列添加元素，需要将所有使用队列的线程先唤醒
	//当有元素push进队列，相当于生产者生产了一个元素
	//若当前没有线程等待条件变量，则唤醒无意义
	bool push(const T &item)
	{
		m_mutex.lock();
		if(m_size >= m_max_size)	// 队列已满
		{
			// 唤醒所有等待的消费者
			m_cond.broadcast();
			m_mutex.unlock();
			return false;
		}

		// 计算新的队尾位置（循环数组）
		m_back = (m_back + 1) % m_max_size;
		// 放入元素
		m_array[m_back] = item;

		m_size++;
		
		// 唤醒可能等待的消费者
		m_cond.broadcast();
		m_mutex.unlock();
		return true;
	}

	// 从队列取出元素（消费者操作，阻塞）
	bool pop(T &item)
	{
		m_mutex.lock();
		// 使用while而不是if，防止虚假唤醒
		while(m_size <= 0)
		{
			// 等待条件变量，会自动释放锁，被唤醒后重新获取锁
			if(!m_cond.wait(m_mutex.get()))
			{
				m_mutex.unlock();
				// 等待失败
				return false;
			}
		}
		
		// 计算新的队首位置（循环数组）
		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;
		m_mutex.unlock();
		return true;
	}

	//增加超时处理
	bool pop(T &item, int ms_timeout)
	{
		struct timespec t = {0,0};
		struct timeval now = {0,0};
		// 获取当前时间
		gettimeofday(&now,NULL);
		m_mutex.lock();
		if(m_size <= 0)
		{
			// 计算绝对超时时间
			t.tv_sec = now.tv_sec + ms_timeout / 1000;
			t.tv_nsec = (ms_timeout % 1000) * 1000;
			// 带超时的等待
			if(!m_cond.timewait(m_mutex.get(),t))
			{
				m_mutex.unlock();
				// 超时或出错
				return false;
			}
		}

		// 再次检查，因为可能在等待期间被唤醒但队列仍为空
		if(m_size <= 0)
		{
			m_mutex.unlock();
			return false;
		}

		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;
		m_mutex.unlock();
		return true;
	}
private:
	locker m_mutex;			// 互斥锁，保护共享数据
	cond m_cond;			// 条件变量，用于线程间通信

	T *m_array;				// 存储队列元素的数组
	int m_size;				// 当前队列元素数量
	int m_max_size;			// 队列最大容量
	int m_front;			// 队首索引
	int m_back;				// 队尾索引
};

#endif
