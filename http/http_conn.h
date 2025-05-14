/*************************************************************************
    > File Name: http_conn.h
    > Author: zsy
    > Created Time: 2025年05月02日 星期五 15时03分40秒
 ************************************************************************/
#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
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
#include <sys/uio.h>
#include <map>

#include "..lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
	static const int FILENAME_LEN = 200;			//文件名最大长度
	static const int READ_BUFFER_SIZE = 2048;		//读缓冲区大小
	static const int WRITE_BUFFER_SIZE = 1024;		//写缓冲区大小
	
	// HTTP请求方法枚举
	enum METHOD
	{
		GET = 0,
		POST,			
		HEAD,
		PUT,
		DELETE,
		TRACE,
		OPTIONS,
		CONNECT,
		PATH
	};

	// HTTP解析状态枚举
	enum CHECK_STATE
	{
		CHECK_STATE_REQUESTLINE = 0,		// 正在解析请求行
		CHECK_STATE_HEADER,					// 正在解析头部字段
		CHECK_STATE_CONTENT					// 正在解析内容
	};

	// HTTP处理结果枚举
	enum HTTP_CODE
	{
		NO_REQUEST,				// 请求不完整，需要继续读取
		GET_REQUEST,			// 获取到一个完整的请求
		BAD_REQUEST,			// 请求语法错误
		NO_RESOURCE,			// 没有资源
		FORBIDDEN_REQUEST,		// 禁止访问
		FILE_REQUEST,			// 文件请求
		INTERNAL_ERROR,			// 服务器内部错误
		CLOSED_CONNECTION		// 连接已关闭
	};
	
	// 行解析状态枚举
	enum LINE_STATUS
	{
		LINE_OK = 0,			// 读取到一个完整的行
		LINE_BAD,				// 行出错
		LINE_OPEN				// 行不完整
	};

public:
	http_conn(){}
	~http_conn(){}

public:
	// 初始化连接
	void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
	// 关闭连接
	void close_conn(bool read_close = true);
	// 处理客户端请求
	void process();
	// 读取客户端数据
	bool read_once();
	// 向客户端写入数据
	bool write();
	// 获取客户端地址
	sockaddr_in *get_address()
	{
		return &m_address;
	}
	//初始化数据库结果集
	void initmysql_result(connection_pool *connPool);
	int timer_flag;			//定时器标志
	int improv;				//改进标志

private:
	// 初始化连接的其他参数
	void init();
	// 解析HTTP请求
	HTTP_CODE process_read();
	// 填充HTTP响应
	bool process_write(HTTP_CODE ret);
	// 解析请求行
	HTTP_CODE parse_request_line(char *text);
	// 解析请求头
	HTTP_CODE parse_headers(char *text);
	// 解析请求内容
	HTTP_CODE parse_content(char *text);
	// 处理请求
	HTTP_CODE do_request();
	// 获取当前行
	char *get_line() { return m_read_buf + m_start_line;}
	// 解析一行
	LINE_STATUS parse_line();
	// 释放内存映射
	void unmap();
	// 添加响应内容(可变参数)
	bool add_response(const char *format, ...);
	// 添加响应内容
	bool add_content(const char *content);
	// 添加状态行
	bool add_status_line(int status, const char *title);
	// 添加头部字段
	bool add_headers(int content_length);
	// 添加内容类型
	bool add_content_type();
	// 添加内容长度
	bool add_content_length(int content_length);
	// 添加连接状态
	bool add_linger();
	// 添加空行
	bool add_blank_line();

public:
	static int m_epollfd;				//epoll文件描述符
	static int m_user_count;			//用户数量
	MYSQL *mysql;						//MySQL连接
	int m_state;	//读为0，写为1		//读为0，写为1

private:
	int m_sockfd;						// 套接字描述符
	sockaddr_in m_address;				// 客户端地址
	char m_read_buf[READ_BUFF_SIZE];	// 读缓冲区
	int m_read_idx;						// 读缓冲区中已经读取的字节数
	long m_checked_idx;					// 正在分析的字符在读缓存区的位置
	int m_start_line;					// 当前正在解析的行的起始位置
	char m_write_buf[WRITE_BUFFER_SIZE];	//写缓冲区
	int m_write_idx;					// 写缓冲区中待发送的字节数
	CHECK_STATE m_check_state;			// 当前解析状态
	METHOD m_method;					// 请求方法 
	char m_real_file[FILENAME_LEN];		// 请求文件的完整路径
	char *m_url;						// 客户端请求的URL路径部分
	char *m_version;					// HTTP版本
	char *m_host;						// 主机名
	long m_content_length;				// 内容长度
	bool m_linger;						// 是否保持连接
	char *m_file_address;				// 文件内存映射地址 
	struct stat m_file_stat;			// 文件状态
	struct iovec m_iv[2];				// 分散写结构
	int m_iv_count;						// 分散数量
	int cgi;							// 是否启用的post
	char *m_string;						// 存储请求头数据
	int bytes_to_send;					// 待发送字节数
	int bytes_have_send;				// 已发送字节数
	char *doc_root;						// 网站根目录

	map<string, string> m_users;		// 用户信息
	int m_TRIGMode;						// 是否关闭日志
	int close_log;						// 数据库用户名
	char sql_user[100];					// 数据库用户名
	char sql_passwd[100];				// 数据库密码
	char sql_name[100];					// 数据库名
};

#endif
