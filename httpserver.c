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
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

#define MAXSIZE 2000

//创建监听套介子，并添加到epoll监听树
int init_listen_fd(int port,int epfd)
{
	//创建监听的套接字
	int lfd = socket(AF_INET,SOCK_STREAM,0);
	if (lfd == -1)
	{
		perror("socket create error");
		exit(1);
	}
	//创建并初始化服务端地址结构
	struct sockaddr_in server_addr;
	bzero(&server_addr,sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	//端口复用
	int opt = 1;
	setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

	//绑定服务器地址结构
	int ret = bind(lfd,(struct sockaddr *)&server_addr,sizeof(server_addr));
	if (ret == -1)
	{
		perror("bind error");
		exit(1);
	}
	//设置监听上限
	ret = listen(lfd,128);
	if (ret == -1)
	{
		perror("listen error");
		exit(1);
	}
	//将lfd挂上epoll监听树
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd;

	ret = epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	if (ret == -1)
	{
		perror("lfd epoll_ctl error");
		exit(1);
	}
	return lfd;

}

//获取一行以\r\n结尾的数据
int get_line(int cfd,char *buf,int size)
{
	int i = 0;
	char c = '\0';
	int n;
	while((i  <  size -1) && (c != '\n'))
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
			}

				
			buf[i] = c;	
			i++;
		}else {
			c = '\n';
		}
	}
	buf[i] = '\0';
	if ( n== -1)
		i= n;
	return i;
}
//断开连接
void disconnect(int cfd,int epfd)
{
	int ret = epoll_ctl(epfd,EPOLL_CTL_DEL,cfd,NULL);
	if (ret != 0)
	{
		perror("epoll_cat_del error");
		exit(1);
	}
	close(cfd);
}

//通过文件名获取文件类型
const char *get_file_type(const char *name)
{
	    char* dot;

	        // 自右向左查找‘.’字符, 如不存在返回NULL
	        dot = strrchr(name, '.');

		if (dot == NULL)
			return "text/plain; charset=utf-8";
		if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
			 return "text/html; charset=utf-8";
		if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
			return "image/jpeg";
		if (strcmp(dot, ".gif") == 0)
			return "image/gif";
		if (strcmp(dot, ".png") == 0)
			return "image/png";
		if (strcmp(dot, ".css") == 0)
			return "text/css";
		if (strcmp(dot, ".au") == 0)
			return "audio/basic";
		if (strcmp( dot, ".wav" ) == 0)
			return "audio/wav";
		if (strcmp(dot, ".avi") == 0)
			return "video/x-msvideo";
		if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
			return "video/quicktime";
		if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
			return "video/mpeg";
		if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
			 return "model/vrml";
		if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
			return "audio/midi";
		if (strcmp(dot, ".mp3") == 0)
			 return "audio/mpeg";
		if (strcmp(dot, ".ogg") == 0)
			return "application/ogg";
		if (strcmp(dot, ".pac") == 0)
			return "application/x-ns-proxy-autoconfig";

	 return "text/plain; charset=utf-8";
}

//回发404页面
void send_error(int cfd, int status, char *title, char *text)
{
	char buf[4096] = {0};

	sprintf(buf, "%s %d %s\r\n", "HTTP/1.1", status, title);
	sprintf(buf+strlen(buf), "Content-Type:%s\r\n", "text/html");
	sprintf(buf+strlen(buf), "Content-Length:%d\r\n", -1);
	sprintf(buf+strlen(buf), "Connection: close\r\n");
	send(cfd, buf, strlen(buf), 0);
	send(cfd, "\r\n", 2, 0);

	memset(buf, 0, sizeof(buf));
	sprintf(buf, "<html><head><link rel='shortcut icon' type='images/x-icon' href='./favicon.ico'><title>%d %s</title></head>\n", status, title);
	sprintf(buf+strlen(buf), "<body bgcolor=\"#cc99cc\"><h2 align=\"center\">%d %s</h4>\n", status, title);
	sprintf(buf+strlen(buf), "%s\n", text);
	sprintf(buf+strlen(buf), "<hr>\n</body>\n</html>\n");
	send(cfd, buf, strlen(buf), 0);
	
	return ;
}

//回发HTTP响应
void send_response(int cfd,int num,const char *status,char *type,int len)
{
	char buf[1024] = {0};
	sprintf(buf,"HTTP/1.1 %d %s\r\n",num,status);
	sprintf(buf+strlen(buf),"%s\r\n",type);
	sprintf(buf+strlen(buf),"Content-Length:%d\r\n",len);

	send(cfd,buf,strlen(buf),0);
	send(cfd,"\r\n",2,0);
}

//回发文件数据
void send_data(int cfd,const char *file)
{
	int n = 0,ret =0;
	char buf[1024] = {0};
	int fd = open(file,O_RDONLY);
	if (fd == -1)
	{
		//给用户显示错误页面
		perror("open failure");
		return;
	}
	while(n=read(fd,buf,sizeof(buf)))
	{
		if (n == -1)
		{
			perror("read file error");
			exit(1);
		}
		int res = send(cfd,buf,n,0);
		if (res == -1)
		{
			if (errno == EAGAIN)
			{
				perror("send file error eagain");
				continue;
			}else if (errno == EINTR)
			{
				perror("send file error eintr");
				continue;
			}else {
				perror("send file error");
				exit(1);
			}
		}				
	}
	close(fd);
}

int  hexit(char c)
{
	if (c>='0' && c<='9')
		return c - '0';
	if (c >= 'a' && c<= 'f')
		return c -'a'+ 10;
	if (c>= 'A' && c<= 'F')
		return c - 'A'+ 10;
	return 0;
}

void encode_str(char *to,int tosize,const char *from)
{
	int tolen;
	for (tolen=0;*from!='\0' && tolen +4 < tosize;++from)
	{
		if (isalnum(*from) ||strchr("/_.-~",*from) != (char *)0)
		{
			*to = *from;
			++to;
			++tolen;
		}else {
			sprintf(to,"%%%%02x",(int)*from & 0xff);
			to += 3;
			tolen += 3;
		}
	}
	*to = '\0';
}

void decode_str(char *to,char *from)
{
	for (;*from != '\0';++to,++from){
		if (from[0] == '%'&& isxdigit(from[2])){
			*to = hexit(from[1])*16 + hexit(from[2]);
			from += 2;
		} else {
			*to = *from;
		}
	}
	*to = '\0';
}

void send_dir(int cfd,const char *dirname)
{
	int i,ret ;
	char buf[1024] = {0};

	sprintf(buf,"<html><head><title>Current Directory：%s</title></head>",dirname);
	sprintf(buf+strlen(buf),"<body><h1>Current Directory :%s</h1><table>",dirname);

	char enstr[1024] = {0};
	char path[1024] = {0};

	//目录项二级指针
	struct dirent** ptr;
	int num = scandir(dirname,&ptr,NULL,alphasort);

	//遍历
	for (i=0;i<num;i++)
	{
		char *name = ptr[i]->d_name;
		//拼接文件的完整路径
		sprintf(path,"%s%s",dirname,name);
		printf("path=%s===============\n",path);
		struct stat st;
		stat(path,&st);
	//编码
	encode_str(enstr,sizeof(enstr),name);
	
	// 如果是文件
        if(S_ISREG(st.st_mode)) {       
            sprintf(buf+strlen(buf), 
                    "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        } else if(S_ISDIR(st.st_mode)) {		// 如果是目录       
            sprintf(buf+strlen(buf), 
                    "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        ret = send(cfd, buf, strlen(buf), 0);
        if (ret == -1) {
            if (errno == EAGAIN) {
                perror("send error:");
                continue;
            } else if (errno == EINTR) {
                perror("send error:");
                continue;
            } else {
                perror("send error:");
                exit(1);
            }
        }
        memset(buf, 0, sizeof(buf));
        // 字符串拼接
    }

    sprintf(buf+strlen(buf), "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);

    printf("dir message send OK!!!!\n");
#if 0
    // 打开目录
    DIR* dir = opendir(dirname);
    if(dir == NULL)
    {
        perror("opendir error");
        exit(1);
    }

    // 读目录
    struct dirent* ptr = NULL;
    while( (ptr = readdir(dir)) != NULL )
    {
        char* name = ptr->d_name;
    }
    closedir(dir);
#endif
}

//处理http请求 
void http_request(int cfd,const char *request)
{
	//拆分http请求行
	char method[16],path[256],protocol[16];
	sscanf(request,"%[^ ] %[^ ] %[^ ]",method,path,protocol);
	printf("method=%s, path=%s, protocol=%s\n",method,path,protocol);

	decode_str(path,path);

	char *file = path+1; 		//去掉path中的/获取访问的文件名
	if (strcasecmp(path,"/")==0)
	{ 	//换为当前目录
		file = "./";
	}	

	struct stat sbuf;
	int ret = stat(file,&sbuf);

	//请求资源不存在
	if (ret != 0)
	{
		//给浏览器回发404页面
		send_error(cfd,404,"Not Found","No such file or directory");
		//perror("open file failure");
		return;
	}

	//请求资源存在
	if (S_ISREG(sbuf.st_mode)) 		//当前资源是文件	
	{
		//回发http响应头
		send_response(cfd,200,"OK",get_file_type(file),sbuf.st_size);
		//回发文件数据
		send_data(cfd,file);

	} else if (S_ISDIR(sbuf.st_mode))	//请求资源是目录
	{
		//回发响应头
		send_response(cfd,200,"OK",get_file_type(".html"),-1);
		//发送当前目录信息
		send_dir(cfd,file);

	}			
}

void do_read(int cfd,int epfd)
{
	//读取一行http协议 拆得到三部分：GET 请求文件名称 版本号
	char line[1024] = {0};
	int len = get_line(cfd,line,sizeof(line));
	if (len == 0)
	{
		printf("客户端已断开连接\n");
		disconnect(cfd,epfd);
	}else {
		printf("=============请求头=============\n");
		printf("请求首行数据:%s\n",line);
		while(1)
		{
			char buf[4096]= {0} ;
			len = get_line(cfd,buf,sizeof(buf));
			if ((len == '\n') || (len == -1))
				break;
		}
	}
	printf("=================The End==============\n");
	if (strncasecmp("GET",line,3) == 0)	//忽略大小写比较前n个字符
	{
		http_request(cfd,line);
		disconnect(cfd,epfd);
	}		
}

void do_accept(int lfd,int epfd)
{
	//创建客户端地址结构
	struct sockaddr_in client_addr;
	socklen_t client_addr_len  = sizeof(client_addr);

	int cfd = accept(lfd,(struct sockaddr *)&client_addr,&client_addr_len);
	if (cfd == -1)
	{
		perror("accept error");
		exit(1);
	}

	//打印客户端的IP+PORT
	char client_ip[64] = {0};
	printf("New client IP:%s, Port:[%d],cfd=%d\n",
			inet_ntop(AF_INET,&client_addr.sin_addr.s_addr,client_ip,sizeof(client_ip)),
			ntohs(client_addr.sin_port),cfd);
	//设置cfd非阻塞
	int flag  = fcntl(cfd,F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd,F_SETFL,flag);

	//将cfd节点挂到监听树上
	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN |EPOLLET;	//边缘触发
	int ret = epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev);
	if (ret == -1)
	{
		perror("cfd epoll_ctl error");
		exit(1);
	}

}

void epoll_run(int port)
{
	int i =0;
	struct epoll_event all_events[MAXSIZE];
	//创建一个epoll监听树
	int epfd = epoll_create(MAXSIZE);
	if (epfd == -1)
	{
		perror("epoll_create error");
		exit(1);
	}
	//创建用于监听的lfd,并添加到监听树
	int lfd = init_listen_fd(port,epfd);
	while(1)
	{
		//监听节点对应事件
		int ret = epoll_wait(epfd,all_events,MAXSIZE,-1);
		if (ret == -1)
		{
			perror("epoll_wait error");
			exit(1);
		}
		for(i=0;i<ret;i++)
		{
			//只处理读事件 其他事件不处理
			struct epoll_event *pev = &all_events[i];
			//不是读事件
			if (!(pev->events & EPOLLIN))
				continue;
			if (pev->data.fd == lfd)//连接事件
			{
				do_accept(lfd,epfd);
			}else {			//读写事件
				do_read(pev->data.fd,epfd);
			}
		}
	}
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
