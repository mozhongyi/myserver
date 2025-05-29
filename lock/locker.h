/*************************************************************************
    > File Name: locker.h
    > Author: sheep
    > Created Time: 2025年05月03日 星期六 15时21分34秒
 ************************************************************************/
#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
	// 默认构造函数，初始化信号量值为0
	sem()
	{
		// 参数: 信号量指针, pshared(0表示线程间共享), 初始值
		if(sem_init(&m_sem, 0, 0) != 0)
		{
			// 初始化失败抛出异常
			throw std::exception();
		}
	}
	// 带参数构造函数，可以指定信号量初始值
	sem(int num)
	{
		if(sem_init(&m_sem, 0, num) != 0)
		{
			throw std::exception();
		}
	}
	// 析构函数，销毁信号量
	~sem()
	{
		sem_destroy(&m_sem);
	}
	// 等待信号量(P操作)，信号量值减1
	bool wait()
	{
		return sem_wait(&m_sem) == 0;
	}
	// 释放信号量(V操作)，信号量值加1
	bool post()
	{
		return sem_post(&m_sem) == 0;
	}

private:
	// POSIX信号量类型
	sem_t m_sem;
};

class locker
{
public:
	// 构造函数，初始化互斥锁
	locker()
	{
		// 第二个参数为属性，NULL表示默认
		if(pthread_mutex_init(&m_mutex, NULL) != 0)
		{
			throw std::exception();
		}
	}
	// 析构函数，销毁互斥锁
	~locker()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	// 加锁操作
	bool lock()
	{
		return pthread_mutex_lock(&m_mutex) == 0;
	}
	
	// 解锁操作
	bool unlock()
	{
		return pthread_mutex_unlock(&m_mutex) == 0;
	}
	// 获取底层互斥锁指针(主要用于配合条件变量使用)
	pthread_mutex_t *get()
	{
		return &m_mutex;
	}
private:
	// POSIX互斥锁类型
	pthread_mutex_t m_mutex;
};

class cond
{
public:
	// 构造函数，初始化条件变量
	cond()
	{
		if(pthread_cond_init(&m_cond, NULL) != 0)
		{
			throw std::exception();
		}
	}
	// 析构函数，销毁条件变量
	~cond()
	{
		pthread_cond_destroy(&m_cond);
	}
	// 等待条件变量(会自动释放关联的互斥锁，并在返回前重新获取)
	bool wait(pthread_mutex_t *m_mutex)
	{
		int ret = 0;
		ret = pthread_cond_wait(&m_cond,m_mutex);
		return ret == 0;
	}
	// 带超时的等待条件变量
	bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
	{
		int ret = 0;
		ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
		return ret == 0;
	}
	// 唤醒一个等待该条件变量的线程
	bool signal()
	{
		return pthread_cond_signal(&m_cond) == 0;
	}
	// 唤醒所有等待该条件变量的线程
	bool broadcast()
	{
		return pthread_cond_broadcast(&m_cond) == 0;
	}

private:
	// POSIX条件变量类型
	pthread_cond_t m_cond;
};

#endif
