/*************************************************************************
  > File Name: main.cpp
  > Author: sheep
  > Created Time: 2025年04月28日 星期一 19时12分11秒
 ************************************************************************/
#include <config.h>

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

	return 0;
}

