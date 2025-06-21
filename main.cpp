/*************************************************************************
  > File Name: main.cpp
  > Author: sheep
  > Created Time: 2025年04月28日 星期一 19时12分11秒
 ************************************************************************/
#include "config.h"

int main(int argc, char* argv[])
{
	//设置数据库信息
	//这是用户名
	string user = "debian-sys-maint";
	string passwd = "7OeckH2fTzPv3hhT";
	string databasename = "yourdb";
	
	//命令行解析
	Config config;
	config.parse_arg(argc, argv);

	WebServer server;

	// 初始化
	server.init(config.PORT, user, passwd, databasename, config.LOGWrite,
				config.OPT_LINGER, config.TRIGMode, config.sql_num,
				config.thread_num, config.close_log, config.actor_model);

	// 初始化日志
	server.log_write();

	// 初始化数据库连接池
	server.sql_pool();

	// 初始化线程池
	server.thread_pool();

	// 触发模式
	server.trig_mode();

	// 监听
	server.eventListen();

	// 运行
	server.eventLoop();

	return 0;
}

