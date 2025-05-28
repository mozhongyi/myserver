/*************************************************************************
    > File Name: log.cpp
    > Author: sheep
    > Created Time: 2025年05月06日 星期二 16时43分45秒
 ************************************************************************/
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
	// 日志行数计数器
	m_count = 0;
	// 默认同步模式
	m_is_async = false;
}

Log::~Log()
{
	if(m_fp != NULL)
	{
		// 确保关闭日志文件
		fclose(m_fp);
	}
}

//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size,int split_lines, int max_queue_size)
{
	//如果设置了max_queue_size,则设置为异步
	if(max_queue_size >= 1)
	{
		m_is_async = true;
		m_log_queue = new block_queue<string>(max_queue_size);
		pthread_t tid;
		//flush_log_thread为回调函数，这里表示创建线程异步写日志
		pthread_create(&tid, NULL, flush_log_thread, NULL);
	}

	// 是否关闭日志
	m_close_log = close_log;
	// 缓冲区大小
	m_log_buf_size = log_buf_size;
	// 分配缓冲区
	m_buf = new char[m_log_buf_size];
	memset(m_buf, '\0', m_log_buf_size);
	// 单个文件最大行数
	m_split_lines = split_lines;

	// 获取当前时间
	time_t t = time(NULL);
	struct tm *sys_tm = localtime(&t);
	struct tm my_tm = *sys_tm;
	
	// 解析文件名路径
	const char *p = strrchr(file_name, '/');
	char log_full_name[256] = {0};
	
	// 构造完整日志文件名
	if(p == NULL)	// 无路径情况
	{
		snprintf(log_full_name, 255, "%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
	}
	// 有路径情况
	else
	{
		strcpy(log_name, p+1);
		strncpy(dir_name, p - file_name + 1);
		snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s",dir_name,my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
	}
	
	// 记录当前日期
	m_today = my_tm.tm_mday;

	// 打开日志文件（追加模式）
	m_fp = fopen(log_full_name,"a");
	if(m_fp == NULL)
	{
		return false;
	}

	return true;
}

// 写入日志的核心函数
void Log::write_log(int level, const char *format, ...)
{
	//获取当前时间(精确到微妙)
	struct timeval now = {0, 0};
	gettimeofday(&now, NULL);
	//转化为本地时间结构
	time_t t = now.tv_sec;
	//localtime返回一个tm结构体指针，该结构体能把时间戳分解为具体的年月日
	struct tm *sys_tm = localtime(&t);
	struct tm my_tm = *sys_tm;
	// 根据日志级别设置前缀
	char s[16] = {0};
	switch(level)
	{
	case 0:
		strcpy(s, "[debug]:");
		break;
	case 1:
		strcpy(s, "[info]:");
		break;
	case 2:
		strcpy(s, "[warn]:");
		break;
	case 3:
		strcpy(s, "[erro]:");
		break;
	default:
		strcpy(s, "[info]:");
		break;
	}
	//加锁保证线程安全
	m_mutex.lock();
	//日志行数计数器递增
	m_count++;

	// 检查是否需要创建新日志文件（跨天或文件行数达到上限）
	if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
	{
		char new_log[256] = {0};
		fflush(m_fp);
		fclose(m_fp);
		char tail[16] = {0};
		
		// 生成日期后缀（年_月_日）
		snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

		// 跨天情况
		if(m_today != my_tm.tm_mday)
		{
			// 生成格式：目录/前缀_年_月_日_日志名
			snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
			// 更新当前日期
			m_today = my_tm.tm_mday;
			// 重置计数器
			m_count = 0;
		}
		else//一个日志文件写满了，需要生成另外的日志文件
		{
			// 生成格式：目录/前缀_年_月_日_日志名.序号
			snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
		}
		// 创建新日志文件
		m_fp = fopen(new_log, "a");
	}

	m_mutex.unlock();
	
	//参数列表变量，用于存储可变参数的信息
	va_list valst;
	va_start(valst, format);

	string log_str;
	m_mutex.lock();

	//写入的具体时间内容格式
	//生成格式如 "2023-08-15 14:30:45.123456 [info]: "
	int n= snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d/%06ld %s",
					my_tm.tm_year+1900, my_tm.tm_mon+1, my_tm.tm_mday,
					my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, 				   s);
	//专门用与处理可变参数列表格式化
	int m = vsnprintf(m_buf + n, m_log_buf_size - n -1, format, valst);
	m_buf[n + m] = '\n';
	m_buf[n + m + 1] = '\0';
	log_str = m_buf;

	m_mutex.unlock();
	
	// 异步模式下且队列未满，加入异步队列
	if(m_is_async && !m_log_queue->full())
	{
		m_log_queue->push(log_str);
	}
	else// 同步模式或队列已满，直接写入文件
	{
		m_mutex.lock();
		fputs(log_str.c_str(),m_fp);
		m_mutex.unlock();
	}
	
	// 清理可变参数列表
	va_end(valst);
}

// 强制刷新缓冲区
void Log::flush(void)
{
	m_mutex.lock();
	//强制刷新写入流缓冲区
	fflush(m_fp);
	m_mutex.unlock();
}
