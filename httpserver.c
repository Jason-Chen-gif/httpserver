#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>

#define MAXSIZE 2000

int do_read(){

}

int main(int argc,char *argv[])
{
	//命令行参数获取 
	if(argc < 3)
		printf("Input format: ./server port path\n");
	//将传入的字符型的port转换为整型
	int port = atoi(argv[1]);
	//改变进程的工作目录
	int ret = chdir(argv[2]);
	if (ret != 0){
		perror("chdir error!");
		exit(1);
	}

	//启动epoll监听
	epoll_run(port);
	return 0;
}
