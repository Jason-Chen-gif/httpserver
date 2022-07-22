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



//获取一行以\r\n结尾的数据
int get_line(int cfd,char *buf,int size)
{
	int i = 0;
	char c = '\0';
	int n;
	while((i < size -1)&&(c!='\n'))
	{
		n = recv(cfd,&c,1,0);
		if (n > 0)
		{
			if (c == '\r')
			{
				n = recv(cfd,&c,1,MSG_PEEK);
				if ((n>0) && (c == '\n')){
					recv(cfd,&c,1,0);
				}else {
					c = '\n';
				}
				buf[i] = c;
				i++;
			}
		}else {
			c = '\n';
		}
	}
	buf[i] = '\0';
	if ( n== -1)
		i= n;
	return i;
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
