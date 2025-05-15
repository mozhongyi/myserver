/*************************************************************************
    > File Name: http-conn.cpp
    > Author: sheep
    > Created Time: 2025年05月12日 星期一 17时29分34秒
 ************************************************************************/
#include "http_conn.h"
#include "mysql/mysql.h"
#include <fstream>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_403_title = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

void http_conn::initmysql_result(connection_pool *connPool)
{
	//先从连接池中取一个连接
	MYSQL *mysql = NULL;
	connectionRAII mysqlcon(&mysql, connPool);

	//在user表中检索username, passwd数据,浏览器输入
	if(mysql_query(mysql, "Select username, passwd FROM user"))
	{
		// mysql_error()返回最后一次MySQL操作的错误描述
		LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
	}

	//从表中检索完整的结果集
	MYSQL_RES *result = mysql_store_result(mysql);

	//返回结果集中的列数
	int num_fields = mysql_num_fields(result);

	//返回所有字段结构的数组
	MYSQL_FIELD *fields = mysql_fetch_fields(result);

	//从结果集中获取下一行,将对应的用户名和密码，存入map中
	while(MYSQL_ROW row = mysql_fetch_row(result))
	{
		string temp1(row[0]);
		string temp2(row[1]);
		users[temp1] = temp2;
	}
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
	epoll_event event;
	event.data.fd = fd;
	
	// 设置事件类型
	if(1 == TRIGMode)
		event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	else
		event.events = EPOLLIN | EPOLLRDHUP;
	
	if(one_shot)
		// EPOLLONESHOT - 一个事件只触发一次，除非用epoll_ctl重置
		event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	//设置文件描述符为非阻塞模式
	setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
	epoll_event event;
	event.data.fd = fd;

	if(1 == TRIGMode)
		event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	else
		event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
	
	// EPOLL_CTL_MOD - 修改已注册的文件描述符的事件
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接，关闭一个连接，客户总量减一
// 参数：read_close - 是否真正需要关闭连接(用于条件性关闭)
void http_conn::close_conn(bool read_close)
{
	// 检查是否需要真正关闭且套接字描述符有效
	if(real_close && (m_sockfd) != -1)
	{
		// 打印关闭的套接字信息(调试用)
		printf("close %d\n", m_sockfd);
		// 将套接字描述符设为无效值(-1)
		removefd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		m_user_count--;
	}
}


//初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname)
{
	// 设置套接字描述符和客户端地址
	m_sockfd = sockfd;
	m_address = addr;
	
	// 将套接字添加到epoll监控
	addfd(m_epollfd, sockfd, true, m_TRIGMode);

	// 增加当前用户连接计数
	m_user_count++;

	//当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中的内容完全为空
	//初始化网站根目录和相关设置
	doc_root = root;
	m_TRIGMode = TRIGMode;
	m_close_log = close_log;

	// 数据库相关参数设置
	strcpy(sql_user, user.c_str());
	strcpy(sql_passwd, passwd.c_str());
	strcpy(sql_name, sqlname.c_str());
	
	// 调用类内部初始化函数
	init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init()
{
	// 重置MySQL连接指针，将在需要时建立连接
	mysql = NULL;
	// 待发送字节数清零
	bytes_to_send = 0;
	// 初始状态：解析请求行
	m_check_state = CHECK_STATE_REQUESTLINE;
	// 默认不保持连接(HTTP Keep-Alive)
	m_linger = false;

	// 默认GET方法
	m_method = GET;
	// 请求URL指针重置
	m_url = 0;
	// HTTP版本指针重置
	m_version = 0;
	// 内容长度清零
	m_content_length = 0;
	// 主机头指针重置
	m_host = 0;

	// 行起始位置重置
	m_start_line = 0;
	// 已解析位置重置
	m_checked_idx = 0;
	// 读缓冲区位置重置
	m_read_idx = 0;
	// 写缓冲区位置重置
	m_write_idx = 0;

	// CGI模式标志重置
	cgi = 0;
	// 连接状态重置
	m_state = 0;
	// 定时器标志重置
	timer_flag = 0;
	// 改进标志重置(用于特殊处理)
	improv = 0;

	// 清空读缓冲区
	memset(m_read_buf, '\0', READ_BUFFER_SIZE);
	// 清空写缓冲区
	memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
	// 清空文件名缓冲区
	memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，用于分析一行内容
// 返回为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
	// 临时存储当前检查的字符
	char temp;
	// 遍历已读取但未检查的数据
	for(; m_checked_idx < m_read_id; ++m_checked_idx)
	{
		// 获取当前字符
		temp = m_read_buf[m_checked_idx];
		// 情况1：遇到回车符\r
		if(temp == '\r')
		{
			// 检查是否是缓冲区末尾(数据不完整)
			if((m_checked_idx + 1) == m_read_idx)
				return LINE_OPEN;
			// 检查是否跟随换行符\n(标准HTTP行结束)
			else if(m_read_buf[m_checked_idx + 1] == '\n')
			{
				// 将\r\n替换为字符串结束符\0\0
				m_read_buf[m_checked_idx++] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				// 成功解析一行
				return LINE_OK;
			}
			// 只有\r没有\n，格式错误
			return LINE_BAD;
		
		}
		// 情况2：遇到换行符\n(可能是前一个缓冲区以\r结束的情况)
		else if(temp == '\n')
		{
			// 检查前面是否是\r(跨缓冲区的行结束)
			if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
			{
				// 将\r\n替换为字符串结束符\0\0
				m_read_buf[m_checked_idx - 1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			// 单独的\n，格式错误
			return LINE_BAD;
		}
	}
	// 遍历完所有数据但没找到行结束符
	return LINE_OPEN;
}

//循环读取客户数据，直到数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
	// 检查读缓冲区是否已满
	if(m_read_idx >= READ_BUFFER_SIZE)
	{
		return false;
	}
	// 记录本次读取的字节数
	int bytes_read = 0;

	// LT读取数据
	if(0 == m_TRIGMode)
	{
		// 调用recv读取数据
		bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
		// 更新已读取数据的索引
		m_read_idx += bytes_read;
		
		// <=0 表示连接关闭或出错
		if(bytes_read <= 0)
		{
			return false;
		}
		
		// 成功读取数据
		return true;
	}
	// ET读数据
	else
	{
		// ET模式需要循环读取，直到读完所有数据
		while(true)
		{
			bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
			// 错误处理
			if(bytes_read == -1)
			{
				// 非阻塞模式下，EAGAIN/EWOULDBLOCK表示数据已读完
				if(errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				return false;
			}
			// 对方关闭连接
			else if(bytes_read == 0)
			{
				return false;
			}
			// 更新已读取数据的索引
			m_read_idx += bytes_read;
		}
		return true;
	}
}

//解析http请求行，获取请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
	// 1. 查找请求方法后的第一个空格或制表符位置
    // strpbrk用于查找字符串中任意匹配字符的位置
	m_url = strpbrk(text, " \t");
	// 如果没有找到空格或制表符，说明请求行格式错误
	if(!m_url)
	{
		// 返回错误请求状态码
		return BAD_REQUEST;
	}

	// 2. 将空格位置设为字符串结束符'\0'，分隔出方法字符串
    // 并将m_url指针移动到方法后的第一个字符
	*m_url++ = '\0';

	// 3. 解析请求方法
	char *method = text;			// method指向请求方法字符串开始位置
	// 不区分大小写比较
	if(strcasecmp(method, "GET") == 0)
		m_method = GET;
	else if(strcasecmp(method, "POST") == 0)
	{
		m_method = POST;
		// 标记需要CGI处理
		cgi = 1;
	}
	else
		return BAD_REQUEST;

	// 4. 跳过URL前的空格和制表符
    // strspn计算连续匹配字符的长度
	m_url += strspn(m_url, " \t");

	// 5. 查找HTTP版本前的空格或制表符
	m_version = strpbrk(m_url, " \t");
	if(!m_version)
		return BAD_REQUEST;

	// 6. 分隔URL和版本号
	*m_version++ = '\0';
	// 跳过版本号前的空白
	m_version += strspn(m_version, " \t");
	
	// 7. 检查HTTP版本是否为1.1
	if(strcasecmp(m_version, "HTTP/1.1") != 0)
		return BAD_REQUEST;

	// 8. 处理URL中的协议部分(http://或https://)
	if(strncasecmp(m_url, "http://", 7) == 0)				// 比较前7个字符
	{
		m_url += 7;				// 跳过"http://"
		m_url = strchr(m_url, '/');							// 查找第一个'/'
	}
	
	// 9. 检查URL是否有效
	if(!m_url || m_url[0] != '/')
		return BAD_REQUEST;						// URL必须以'/'开头
	
	//当url为/时显示判断界面
	if(strlen(m_url) == 1)
		strcat(m_url, "judge.html");

	// 11. 改变状态机状态，准备解析头部字段
	m_check_state = CHECK_STATE_HEADER;

	// 12. 返回请求不完整状态，等待继续解析
	return NO_REQUEST;

}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
	// 检查空行，头部结束的标志
	if(text[0] == '\0')
	{
		// 如果内容长度不为0，说明还有请求体需要读取
		if(m_content_length != 0)
		{
			// 转换状态到检查内容体状态
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		// 如果没有请求体，则头部解析完成，获得完整请求
		return GET_REQUEST;
	}
	// 解析Connection头部
	else if(strncasecmp(text, "Connection:", 11) == 0)
	{
		// 跳过"Connection:"
		text += 11;
		// 跳过可能存在的空白字符(空格和制表符)
		text += strspn(text, " \t");
		// 检查是否为keep-alive连接
		if(strcasecmp(text, "keep-alive") == 0)
		{
			// 设置长连接标志
			m_linger = true;
		}
	}
	// 解析Content-length头部
	else if(strncasecmp(text, "Content-length:", 15) == 0)
	{
		// 跳过"Content-length:"
		text += 5;
		// 跳过空白字符
		text += strspn(text, " \t");
		// 直接存储主机名指针
		m_host = text;
	}
	// 未知头部处理
	else
	{
		// 记录未知头部信息到日志
		LOG_INFO("oop!unknow header: %s", text);
	}
	// 默认返回请求未完成状态
	return NO_REQUEST;
}

//判断http请求是否被完整呢个读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
	// 检查已读取的数据量是否达到内容长度
	if(m_read_idx >= (m_content_length + m_checked_idx))
	{
		// 在内容体末尾添加字符串结束符
		text[m_content_length] = '\0';
		//POST请求中最后为输入的用户名和密码
		m_string = text;
		return GET_REQUEST;
	}
	// 返回获取完整请求的状态
	return NO_REQUEST;
}

// 处理读取到的HTTP请求数据
http_conn::HTTP_CODE http_conn::process_read()
{
	// 当前行解析状态
	LINE_STATUS line_status = LINE_OK;
	// HTTP请求处理结果
	HTTP_CODE ret = NO_REQUEST;
	// 当前处理的行文本
	char *text = 0;
	
	// 主解析循环：处理内容体或按行解析
	while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status == parse_line()) == LINE_OK))
	{
		// 获取当前行文本
		text = get_line();
		// 更新起始行位置
		m_start_line = m_checked_idx;
		// 记录日志
		LOG_INFO("%s", text);

		// 根据当前解析状态处理
		switch(m_check_state)
		{
		case CHECK_STATE_REQUESTLINE:				// 解析请求行
		{
			ret = parse_request_line(text);
			if(ret == BAD_REQUEST)
				return BAD_REQUEST;
			break;
		}
		case CHECK_STATE_HEADER:					// 解析头部
		{
			ret = parse_headers(text);
			if(ret == BAD_REQUEST)
				return BAD_REQUEST;
			else if(ret == GET_REQUEST)				// 头部解析完成
			{
				return do_request();				// 执行请求处理
			}
			break;
		}
		case CHECK_STATE_CONTENT:					// 解析内容体
		{
			ret = parse_content(text);
			if(ret == GET_REQUEST)					// 内容体解析完成
				return do_request();				// 执行请求处理
			line_status = LINE_OPEN;				// 标记行状态为开放(继续读取)
			break;
		}
		default:
			return INTERNAL_ERROR;
		}
	}
	// 请求未完成，需要继续读取数据
	return NO_REQUEST;
}

// 处理HTTP请求的核心函数，返回HTTP状态码
http-conn::HTTP_CODE http_conn::do_request()
{
	// 初始化文件路径：将文档根目录复制到m_real_file
	strcpy(m_real_file, doc_root);
	int len = strlen(doc_root);
	// 获取URL中最后一个'/'后面的部分
	const char *p = strrchr(m_url, '/');

	//处理cgi(登陆/注册动态请求)
	if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
	{
		// 根据URL判断是登录('2')还是注册('3')
		char flag = m_url[1];
		
		// 分配内存并构造新的URL路径
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/");
		// 跳过标志字符
		strcat(m_url_real, m_url + 2);
		strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len -1);
		free(m_url_real);

		//将用户名和密码提取出来
		//user=123&&passwd=123
		char name[100], password[100];
		int i;
		// 解析用户名 (跳过"user="这5个字符)
		for(i = 5; m_string[i] != '&'; ++i)
			name[i - 5] = m_string[i];
		name[i - 5] = '\0';

		int j = 0;
		// 解析密码 (跳过"&passwd="这8个字符)
		for(i = i + 10; m_string[i] != '\0'; ++i,++j)
			password[j] = m_string[i];
		password[j] = '\0';
		
		// 处理注册请求
		if(*(p + 1) == '3')
		{
			//如果是注册，先检测数据库是否有重名
			//没有重名，进行增加数据
			char *sql_insert = (char *)malloc(sizeof(char) * 200);
			strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
			strcat(sql_insert, "'");
			strcat(sql_insert, name);
			strcat(sql_insert, "', '");
			strcat(sql_insert, password);
			strcat(sql_insert, "')");
			
			// 检查用户名是否已存在
			if(users.find(name) == users.end())
			{
				// 加锁保护数据库操作和内存中的用户表
				m_lock.lock();
				int res = mysql_query(mysql, sql_insert);
				users.insert(pair<string, string>(name, password));
				m_lock.unlock();

				// 根据数据库操作结果重定向不同页面
				if(!res)
					// 注册成功跳转到登录页
					strcpy(m_url, "/log.html");
				else
					// 注册失败
					strcpy(m_url, "/registerError.html");
			}
			else
				// 用户名已存在
				strcpy(m_url, "/registerError.html");
		}
		//如果是登陆，直接判断
		//若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
		else if(*(p + 1) == '2')
		{
			// 检查用户名是否存在且密码匹配
			if(users.find(name) != users.end() && users[name] == password)
				// 登录成功
				strcpy(m_url, "welcome.html");
			else
				// 登录失败
				strcpy(m_url, "logError.html");
		}
	}
	
	// 处理静态页面请求
	// 根据URL中的数字标识映射到不同的HTML页面
	if(*(p + 1) == '0')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/register.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if(*(p + 1) == '1')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/log.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if(*(p + 1) == '5')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/picture.html");
		strcpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if(*(p + 1) == '6')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/video.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if(*(p + 1) == '7')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/fans.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
		
		free(m_url_real);
	}
	else
		// 默认情况：直接使用URL作为文件路径
		strncpy(m_real_file + len, m_url, FILENAME_LEN - len -1);
	
	// 检查文件状态
	if(stat(m_real_file, &m_file_stat) < 0)
		// 文件不存在
		return NO_RESOURCE;

	// 检查文件权限
	if(!(m_file_stat.st_mode & S_IROTH))
	{	
		// 没有读取权限
		return FORBIDDEN_REQUEST;
	}
	
	// 检查是否是目录
	if(S_ISDIR(m_file_stat.st_mode))
		// 请求的是目录而非文件
		return BAD_REQUEST;
	
	// 内存映射文件内容
	int fd = open(m_real_file, O_RDONLY);
	m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	// 文件请求成功
	return FILE_REQUEST;
}

// 取消内存映射
void http_conn::unmap()
{
	// 检查是否有有效的内存映射地址
	if(m_file_address)
	{
		// 使用munmap系统调用取消内存映射
        // 参数1: 映射的内存起始地址
        // 参数2: 映射的内存区域大小(从文件状态中获取)
		munmap(m_file_address, m_file_stat.st_size);
		// 将指针置为0/NULL，避免成为悬垂指针
		m_file_address = 0;
	}
}

// 向客户端发送数据的函数
bool http_conn::write()
{
	// 临时变量，记录每次writev的返回值
	int temp = 0;
	// 如果没有数据要发送，重新初始化并返回
	if(bytes_to_send == 0)
	{
		// 修改epoll事件为可读
		modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
		// 重新初始化连接状态
		init();
		return true;
	}
	
	// 循环发送数据直到全部发送完毕或出错
	while(1)
	{
		// 使用writev分散写，同时发送头部和数据
		temp = writev(m_sockfd, m_iv, m_iv_count);
	
		// 发送出错处理
		if(temp < 0)
		{
			// 如果是EAGAIN错误(写缓冲区满)，等待下次可写事件
			if(errno == EAGAIN)
			{
				// 注册可写事件
				modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
				return true;
			}
			// 其他错误则取消内存映射并返回失败
			unmap();
			return false;
		}
		
		// 更新已发送和待发送字节数
		bytes_have_send += temp;
		bytes_to_send -= temp;

		// 调整iovec结构
		if(bytes_have_send >= m_iv[0].iov_len)			// 头部已全部发送完
		{
			// 第一个iovec长度设为0
			m_iv[0].iov_len = 0;
			// 调整文件数据的起始位置和长度
			m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
			m_iv[1].iov_len = bytes_to_send;
		}
		// 头部未发送完
		else
		{
			// 调整头部数据的起始位置和剩余长度
			m_iv[0].iov_base = m_write_buf + bytes_have_send;
			m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
		}
		
		// 检查是否全部发送完毕
		if(bytes_to_send <= 0)
		{
			// 取消内存映射
			unmap();
			// 重新注册可读事件
			modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
			
			// 根据Connection字段决定是否保持连接
			if(m_linger)		//保持连接 
			{
				// 重新初始化连接状态
				init();
				return true;
			}
			// 关闭连接
			else
			{
				return false;
			}
		}
	}
}

// 向HTTP响应缓冲区添加格式化数据
bool http_conn::add_response(const char *format, ...)
{
	// 检查当前写入位置是否已超出或达到缓冲区上限
    // 防止后续操作导致缓冲区溢出
	if(m_write_idx >= WRITE_BUFFER_SIZE)
		return false;

	// 定义可变参数列表
	va_list arg_list;
	// 初始化可变参数列表，从format之后开始获取参数
	va_start(arg_list, format);
	// 使用vsnprintf安全地格式化输出到缓冲区
    // m_write_buf + m_write_idx 是写入的起始位置
    // WRITE_BUFFER_SIZE-1-m_write_idx 是剩余可用缓冲区大小（-1 为了给字符串结尾的'\0'留空间
	int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, arg_list);
	// 检查格式化后的字符串长度是否超出剩余缓冲区空间
    // 如果超出，清理资源并返回失败
	if(len >= (WRITE_BUFFER_SIZE-1-m_write_idx))
	{
		// 释放可变参数列表资源
		va_end(arg_list);
		return false;
	}

	// 更新写入位置索引，将其增加已写入数据的长度
	m_write_idx += len;
	// 释放可变参数列表资源，避免内存泄漏
	va_end(arg_list);
	
	// 记录日志，输出完整的响应内容（包括已写入的所有响应）
	LOG_INFO("request:%s", m_write_buf);

	return true;
}

// 添加HTTP响应的状态行
bool http_conn::add_status_line(int status, const char *title)
{
	// 格式示例："HTTP/1.1 200 OK\r\n"
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加HTTP响应头部的多个标准字段
bool http_conn::add_headers(int conten_len)
{
	// 依次添加三个标准HTTP头部字段，使用短路与(&&)确保顺序执行：
    // 1. 添加Content-Length头部（内容长度）
    // 2. 添加Connection头部（连接保持选项）
    // 3. 添加空行（分隔头部和正文）
    // 只有所有操作都成功才返回true
	return add_content_length(content_len) && add_linger() && add_blank_line();
}

// 添加HTTP响应头中的Content-Length字段
bool http_conn::add_content_length(int content_len)
{
	return add_response("Content-Length:%d\r\n", content_len);
}

// 添加HTTP响应头中的Content-Type字段
bool http_conn::add_content_type()
{
	return add_response("Content-Type:%s\r\n","text/html");
}

// 添加HTTP响应头中的Connection字段
bool http_conn::add_linger()
{
	return add_response("Connection:%s\r\n",(m_linger == true) ? "keep-alive" : "close");
}

// 添加HTTP响应头结束的空行
bool http_conn::add_blank_line()
{
	return add_response("%s", "\r\n");
}

// 添加HTTP响应正文内容
bool http_conn::add_content(const char *content)
{
	return add_response("%s", content);
}

// 根据HTTP处理结果生成对应的响应报文
bool http_conn::process_write(HTTP_CODE ret)
{
	switch(ret)
	{
	// 500 服务器内部错误
	case INTERNAL_ERROR:
	{
		// 添加状态行: HTTP/1.1 500 Internal Error
		add_status_line(500, error_500_title);
		// 添加头部，包含错误页面长度
		add_headers(strlen(error_500_form));
		// 添加错误页面内容
		if(!add_content(error_500_form))
		{
			return false;
		}
		break;
	}
	// 404 资源未找到
	case BAD_REQUEST:
	{
		add_status_line(404, error_404_title);
		add_headers(strlen(error_404_form));
		if(!add_content(error_404_form))
			return false;
		break;
	}
	// 403 禁止访问
	case FORBIDDEN_REQUEST:
	{
		add_status_line(403, error_403_title);
		add_headers(strlen(error_403_form));
		if(!add_content(error_404_form))
			return false;
		break;
	}
	// 200 文件请求成功
	case FILE_REQUEST:
	{
		add_status_line(200, ok_200_title);
		// 文件存在且非空
		if(m_file_stat.st_size != 0)
		{
			add_headers(m_file_stat.st_size);
			// 设置iovec结构，准备分散写(内存缓冲区+文件内容)
			m_iv[0].iov_base = m_write_buf;				// 响应头部分
			m_iv[0].iov_len = m_write_idx;				// 头部长度
			m_iv[1].iov_base = m_file_address;			// 文件内容部分
			m_iv[1].iov_len = m_file_stat.st_size;		// 文件长度
			m_iv_count = 2;								// 两个缓冲区
			bytes_to_send = m_write_idx + m_file_stat.st_size;	// 总发送字节数
			return true;
		}
		// 文件为空的情况
		else
		{
			const char *ok_string = "<html><body></body></html>";
			add_headers(strlen(ok_string));
			if(!add_content(ok_string))
				return false;
		}
	}
	default:
		return false;
	}

	// 设置普通响应(非文件)的iovec结构
	// 整个响应都在内存缓冲区
	m_iv[0].iov_base = m_write_buf;
	// 缓冲区长度
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;
	// 发送总字节数
	bytes_to_send = m_write_idx;
	return true;
}

// 处理HTTP连接的主流程控制函数
void http_conn::process()
{
	// 处理HTTP请求读取和解析
	HTTP_CODE read_ret = process_read();
	// 如果请求不完整，需要继续读取数据
	if(read_ret == NO_REQUEST)
	{
		// 修改epoll事件为可读，等待更多数据到达
		modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
		return;
	}
	// 根据解析结果生成HTTP响应
	bool write_ret = process_write(read_ret);
	// 如果响应生成失败，关闭连接
	if(!write_ret)
	{
		close_conn();
	}
	// 准备发送响应数据，设置epoll事件为可写
	modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
