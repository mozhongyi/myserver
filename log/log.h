/*************************************************************************
    > File Name: log.h
    > Author: sheep
    > Created Time: 2025年05月05日 星期一 15时31分33秒
 ************************************************************************/
#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
	//C++11以后，使用局部变量懒汉不用加锁
	static Log *get_instance()
	{
		// C++11保证线程安全
		static Log instance;
		return &instance;
	}

	// 异步日志刷新线程的入口函数
	static void *flush_log_thread(void *args)
	{
		Log::get_instance()->async_write_log();
	}

	//可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
	bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
	
	// 写入日志（可变参数）
	void write_log(int level, const char *format, ...);
	
	// 强制刷新缓冲区
	void flush(void);

private:
	// 私有构造函数（单例模式）
	Log();
	virtual ~Log();

	// 异步写入日志的工作函数
	void *async_write_log()
	{
		string single_log;
		//从阻塞队列中取出一个日志string,写入文件
		while(m_log_queue->pop(single_log))
		{
			m_mutex.lock();
			// 写入文件
			fputs(single_log.c_str(),m_fp);
			m_mutex.unlock();
		}
	}

private:
	char dir_name[128];				// 日志文件路径
	char log_name[128];				// 日志文件名
	int m_split_lines;				// 日志最大行数
	int m_log_buf_size;				// 日志缓冲区大小
	long long m_count;				// 当前日志文件行数计数
	int m_today;					// 当前日期（用于按天分割）
	FILE *m_fp;						// 日志文件指针
	char *m_buf;					// 日志缓冲区
	block_queue<string> *m_log_queue;	// 异步日志队列
	bool m_is_async;			// 是否同步标志位
	locker m_mutex;				// 互斥锁（保护文件写入）
	int m_close_log;			// 日志系统关闭标志
};

// 日志宏定义（方便使用）
// ##_VA_ARGS__可以优化可变参数为空的问题
#define LOG_DEBUG(format, ...) if(0 == m_close_log)	{Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif

