#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "parse_metafile.h"
#include "peer.h"
#include "tracker.h"

extern unsigned char info_hash[20];
extern unsigned char peer_id[20];
extern Announce_list *announce_list_head;

extern int *sock;
extern struct sockaddr_in *tracker;
extern int *valid;
extern int tracker_count;

extern int *peer_sock;
extern struct sockaddr_in *peer_addr;
extern int *peer_valid;
extern int peer_count;

Peer_addr *peer_addr_head = NULL;

/*
*功能：根据HTTP协议进行编码转换
*传入参数：
*	*in 
*	len1
*传出参数：
*	*out
*	len2
*返回值：
* -1	错误
* 0
*/
int http_encode(unsigned char *in,int len1,char *out,int len2)
{
	int  i, j;
	char hex_table[16] = "0123456789abcdef"; 
	
	if( (len1 != 20) || (len2 <= 90) )  
	{
		return -1;
	}	
	
	for(i = 0, j = 0; i < 20; i++, j++) 
	{
		if( isalpha(in[i]) || isdigit(in[i]) )
		{//英文字符
			out[j] = in[i];
		}
		else 
		{ 
			out[j] = '%';
			j++;
			out[j] = hex_table[in[i] >> 4];//取出ASCII码的高四位
			j++;
			out[j] = hex_table[in[i] & 0xf];//取出ASCII码的低四位
		}
	}
	out[j] = '\0';
	
#ifdef DEBUG
	//printf("http encoded:%s\n",out);
#endif
	
	return 0;
}

/*
*功能：获取Tracker URL中的主机名部分
*传入参数：
*	*node 保存Tracker的URL链表节点
*	len
*传出参数：
* *name主机名称
*返回值：
* -1 出错
* 0
*/

int get_tracker_name(Announce_list *node,char *name,int len)
{
	int i = 0, j = 0;

	if( (len < 64) || (node == NULL) )  
	{
		return -1;
	}
		
	if( memcmp(node->announce,"http://",7) == 0 )
	{//略过"http://"
		i = i + 7;
	}
		
	while( (node->announce[i] != '/') && (node->announce[i] != ':') ) 
	{
		name[j] = node->announce[i];
		i++;
		j++;
		
		if( i == strlen(node->announce) ) 
		{			
			break;
		}
	}
	name[j] = '\0';

#ifdef DEBUG
	printf("%s\n",node->announce);
	printf("tracker name:%s\n",name);
#endif

	return 0;
}

/*
*功能：获取Tracker URL中的端口号
*传入参数：
*	*node 保存Tracker的URL链表节点
*传出参数：
* *port	端口号
*返回值：
* -1 
* 0
*/

int get_tracker_port(Announce_list *node,unsigned short *port)
{
	int i = 0;

	if( (node == NULL) || (port == NULL) )  
	{
		return -1;
	}	
	
	if( memcmp(node->announce,"http://",7) == 0 )  
	{
		i = i + 7;
	}
		
	*port = 0;//初始化
	
	while( i < strlen(node->announce) ) 
	{
		if( node->announce[i] != ':')   
		{ 
			i++; 
			continue; 
		}

		i++;  // skip ':'
		
		while( isdigit(node->announce[i]) ) 
		{ 
			*port =  *port * 10 + (node->announce[i] - '0');
			i++;
		}
		break;
	}
	if(*port == 0)  
	{
		*port = 80;
	}

#ifdef DEBUG
	printf("tracker port:%d\n",*port);
#endif

	return 0;
}

/*
*功能：构造发送到Tracker服务器的HTTP GET请求
*传入参数：
*						numwant是希望Tracker返回的Peer数
*						node为指向Tracker的URL
*						port为监听的端口号
*						down为已下载的数据量
*						up为已上传的数据量
*						left为剩余多少字节未下载
*传出参数：
*						*request用于接收生成的请求
*						len为request所指向的数组的长度
*返回值：
* 					-1 
* 					0
*/

int create_request(char *request,int len,Announce_list *node,
				   unsigned short port,long long down,long long up,
				   long long left,int numwant)
{
	char encoded_info_hash[100];
	char encoded_peer_id[100];
	int key;
	char tracker_name[128];
	unsigned short tracker_port;

	//HTTP协议编码转换
	//将种子文件中的hash值进行HTTP协议编码转换
	//将随机值peer_id进行HTTP协议编码转换
	http_encode(info_hash,20,encoded_info_hash,100);
	http_encode(peer_id,20,encoded_peer_id,100);

	//产生一个随机数来标识客服端
	srand(time(NULL));
	key = rand() / 10000;

	//获取主机名和端口号
	get_tracker_name(node,tracker_name,128);
	get_tracker_port(node,&tracker_port);

	sprintf(request,
	"GET /announce?info_hash=%s&peer_id=%s&port=%u"
	"&uploaded=%lld&downloaded=%lld&left=%lld"
	"&event=started&key=%d&compact=1&numwant=%d HTTP/1.0\r\n"
	"Host: %s\r\nUser-Agent: Bittorrent\r\nAccept: */*\r\n"
	"Accept-Encoding: gzip\r\nConnection: closed\r\n\r\n",
	encoded_info_hash,encoded_peer_id,port,up,down,left,
	key,numwant,tracker_name);

#ifdef DEBUG
	printf("request:%s\n",request);
#endif

	return 0;
}

/*功能：获取Tracker返回的消息
*传入参数：
*					buffer指向Tracker的回应消息
*					len为buffer所指向的数组的长度
*传出参数：
*					total_length用于存放Tracker返回数据的长度
*返回值：
* -1
* 0	第一种消息类型
* 1
*/
int get_response_type(char *buffer,int len,int *total_length)
{
	int i, content_length = 0;

	//查找关键字"5:peers"
	for(i = 0; i < len-7; i++) 
	{
		if(memcmp(&buffer[i],"5:peers",7) == 0) 
		{ 
			i = i+7;//跳过"5:peers"
			break; 
		}
	}
	if(i == len-7) 
	{		
		return -1;  // 返回的消息不含"5:peers"关键字
	}
		
	if(buffer[i] != 'l')  
	{
		return 0;   // 返回的消息的类型为第一种
	}

	*total_length = 0;
	//查找关键字"Content-Length: "
	for(i = 0; i < len-16; i++) 
	{
		if(memcmp(&buffer[i],"Content-Length: ",16) == 0) 
		{
			i = i+16;//跳过"Content-Length: "
			break; 
		}
	}
	
	if(i != len-16) 
	{
		while(isdigit(buffer[i])) 
		{
			//获得Tracker返回信息的长度
			content_length = content_length * 10 + (buffer[i] - '0');
			i++;
		}
		
		//查找关键字"\r\n\r\n"
		for(i = 0; i < len-4; i++) 
		{
			if(memcmp(&buffer[i],"\r\n\r\n",4) == 0)  
			{ 
				i = i+4;//跳过"\r\n\r\n"
				break; 
			}
		}
		
		//获得Tracker返回信息的总长度
		if(i != len-4) 
		{			
			*total_length = content_length + i;
		}
	}

	if(*total_length == 0)  
	{
		return -1;
	}
	else 
	{
		return 1;
	}
}


/*
hostent结构说明如下：
	struct  hostent
	{
	    char *h_name;           //正式的主机名称
	    char **h_aliases;       //指向主机名称的其他别名
	    int h_addrtype;         //地址的型态， 通常是AF_INET 
	    int h_length;           //地址的长度
	    char **h_addr_list;     //从域名服务器取得该主机的所有地址
	};
*/
/*
返回值 ：成功返回hostent结构指针，失败则返回NULL指针， 错误原因存于h_errno变量中
错误代码：
                HOST_NOT_FOUND                找不到指定的主机
                NO_ADDRESS                    该主机有名称却无IP地址
                NO_RECOVERY                   域名服务器有错误发生
                TRY_AGAIN                     请再调用一次
*功能：以非阻塞的方式连接Tracker
*传入参数：
*传出参数：
*返回值：
*/
int prepare_connect_tracker(int *max_sockfd)
{
	int i, flags, ret, count = 0;
	struct hostent  *ht;
	Announce_list *p = announce_list_head;

	while(p != NULL)  
	{ 
		count++; 
		p = p->next; 
	}
	
	tracker_count = count;//获取Tracker的URL的数量
	//申请存储socket文件描述符的堆空间
	sock = (int *)malloc(count * sizeof(int));
	
	if(sock == NULL)  
		goto OUT;
		
	//申请存储sockaddr_in 结构体堆空间
	tracker = (struct sockaddr_in *)malloc(count * sizeof(struct sockaddr_in));
	
	if(tracker == NULL)  
		goto OUT;
		
	valid = (int *)malloc(count * sizeof(int));
	
	if(valid == NULL)  
		goto OUT;
	
	p = announce_list_head;//获取保存URL的链表头指针
	for(i = 0; i < count; i++) 
	{
		char tracker_name[128];
		unsigned short tracker_port = 0;
		
		//创建count个通信节点
		sock[i] = socket(AF_INET,SOCK_STREAM,0);
		if(sock < 0) 
		{
			printf("%s:%d socket create failed\n",__FILE__,__LINE__);
			valid[i] = 0;
			p = p->next;
			continue;
		}

		//获取Tracker的URL
		get_tracker_name(p,tracker_name,128);
		//获取端口号
		get_tracker_port(p,&tracker_port);
		
		// 从主机名获取IP地址
		ht = gethostbyname(tracker_name);
		if(ht == NULL) 
		{
			printf("gethostbyname failed:%s\n",hstrerror(h_errno)); 
			valid[i] = 0;
		} 
		else 
		{
			//清除sockaddr_in结构体空间
			memset(&tracker[i], 0, sizeof(struct sockaddr_in));
			//拷贝IP地址、端口号、协议族到sockaddr_in结构体中
			memcpy(&tracker[i].sin_addr.s_addr, ht->h_addr_list[0], 4);
			tracker[i].sin_port = htons(tracker_port);
			tracker[i].sin_family = AF_INET;
			valid[i] = -1;
		}
		
		p = p->next;
	}

	for(i = 0; i < tracker_count; i++) 
	{
		if(valid[i] != 0) 
		{
			if(sock[i] > *max_sockfd) 
			{
				*max_sockfd = sock[i];
			}
			
			//获取文件访问模式
			flags = fcntl(sock[i],F_GETFL,0);
			// 设置套接字为非阻塞
			fcntl(sock[i],F_SETFL,flags|O_NONBLOCK);
			
			// 连接tracker
			ret = connect(sock[i],(struct sockaddr *)&tracker[i],
				          sizeof(struct sockaddr));
				          
			if(ret < 0 && errno != EINPROGRESS) 
			{				
				valid[i] = 0;	
			}
			// 如果返回0，说明连接已经建立
			if(ret == 0)  
			{
				valid[i] = 1;  
			}
		}
	}

	return 0;

OUT:
	if(sock != NULL) free(sock);
	if(tracker != NULL) free(tracker);
	if(valid != NULL) free(valid);
	return -1;
}


/*
*功能：以非阻塞的方式连接peer
*传入参数：
*传出参数：
*返回值：
*/
int prepare_connect_peer(int *max_sockfd)
{
	int       i, flags, ret, count = 0;
	Peer_addr *p;
	
	p = peer_addr_head;//获得保存PeerIP地址和端口号的链表头指针
	while(p != 0)  
	{ 
		count++; 
		p = p->next; 
	}

	peer_count = count;
	peer_sock = (int *)malloc(count*sizeof(int));
	if(peer_sock == NULL)  goto OUT;
		
	peer_addr = (struct sockaddr_in *)malloc(count*sizeof(struct sockaddr_in));
	if(peer_addr == NULL)  goto OUT;
		
	peer_valid = (int *)malloc(count*sizeof(int));
	if(peer_valid == NULL) goto OUT;
	
	p = peer_addr_head;  // 此处p重新赋值
	for(i = 0; i < count && p != NULL; i++) 
	{
		//创建count个通信节点并获得文件描述符
		peer_sock[i] = socket(AF_INET,SOCK_STREAM,0);
		if(peer_sock[i] < 0) 
		{ 
			printf("%s:%d socket create failed\n",__FILE__,__LINE__);
			valid[i] = 0;
			p = p->next;
			continue; 
		}

		memset(&peer_addr[i], 0, sizeof(struct sockaddr_in));
		peer_addr[i].sin_addr.s_addr = inet_addr(p->ip);
		peer_addr[i].sin_port = htons(p->port);
		peer_addr[i].sin_family = AF_INET;
		peer_valid[i] = -1;
		
		p = p->next;
	}
	count = i;
	
	for(i = 0; i < count; i++) 
	{
		if(peer_sock[i] > *max_sockfd) 
			*max_sockfd = peer_sock[i];
		// 设置套接字为非阻塞
		flags = fcntl(peer_sock[i],F_GETFL,0);
		fcntl(peer_sock[i],F_SETFL,flags|O_NONBLOCK);
		// 连接peer
		ret = connect(peer_sock[i],(struct sockaddr *)&peer_addr[i],
			          sizeof(struct sockaddr));
		if(ret < 0 && errno != EINPROGRESS)  
			peer_valid[i] = 0;
		// 如果返回0，说明连接已经建立
		if(ret == 0)  
			peer_valid[i] = 1;
	}
	
	free_peer_addr_head();
	return 0;

OUT:
	if(peer_sock  != NULL)  free(peer_sock);
	if(peer_addr  != NULL)  free(peer_addr);
	if(peer_valid != NULL)  free(peer_valid);
	return -1;
}

/*
*功能:解析第一种Tracker的回应消息
*传入参数：
*传出参数：
*返回值：
*
*/
int parse_tracker_response1(char *buffer,int ret,char *redirection,int len)
{
	int           i, j, count = 0;
	unsigned char c[4];
	Peer_addr     *node, *p;

	//查找关键字"Location: "
	for(i = 0; i < ret - 10; i++) 
	{
		if(memcmp(&buffer[i],"Location: ",10) == 0) 
		{ 
			i = i + 10;//跳过"Location: "
			j = 0;
			while(buffer[i]!='?' && i<ret && j<len) 
			{
				redirection[j] = buffer[i];
				i++;
				j++;
			}
			redirection[j] = '\0';
			return 1;
		}
	                                                                                                                                        
	}

	// 获取返回的peer数,关键词"5:peers"之后为各个Peer的IP和端口
	for(i = 0; i < ret - 7; i++) 
	{
		if(memcmp(&buffer[i],"5:peers",7) == 0)
		{ 
			i = i + 7; 
			break; 
		}
	}
	
	if(i == ret - 7	) 
	{ 
		printf("%s:%d can not find keyword 5:peers \n",__FILE__,__LINE__);
		return -1; 
	}
	
	while( isdigit(buffer[i]) ) 
	{
		count = count * 10 + (buffer[i] - '0');
		i++;
	}
	i++;  // 跳过":"

	//获得IP地址和端口的数量
	count = (ret - i) / 6;
		
	// 将每个peer的IP和端口保存到peer_addr_head指向的链表中
	for(; count != 0; count--) 
	{
		node = (Peer_addr*)malloc(sizeof(Peer_addr));
		
		c[0] = buffer[i];   
		c[1] = buffer[i+1]; 
		c[2] = buffer[i+2];
		c[3] = buffer[i+3];
		 
		sprintf(node->ip,"%u.%u.%u.%u",c[0],c[1],c[2],c[3]);
		i += 4;
		node->port = ntohs(*(unsigned short*)&buffer[i]);
		i += 2;
		node->next = NULL;
	
		// 判断当前peer是否已经存在于链表中
		p = peer_addr_head;
		while(p != NULL) 
		{
			if( memcmp(node->ip,p->ip,strlen(node->ip)) == 0 ) 
			{ 
				//已存在释放当前结点不进行存储
				free(node); 
				break;
			}
			p = p->next;
		}
			
		// 将当前结点添加到链表中
		if(p == NULL) 
		{
			if(peer_addr_head == NULL)
				peer_addr_head = node;
			else 
			{
				p = peer_addr_head;
				while(p->next != NULL) 
					p = p->next;
				p->next = node;
			}
		}
	}
		
#ifdef DEBUG
		count = 0;
		p = peer_addr_head;
		while(p != NULL) 
		{
			printf("+++ connecting peer %-16s:%-5d +++ \n",p->ip,p->port);
			p = p->next;
			count++;
		}
		printf("peer count is :%d \n",count);
#endif

		return 0;
}

/*
*功能:解析第二种Tracker的回应消息
*传入参数：
*传出参数：
*返回值：
*/

int parse_tracker_response2(char *buffer,int ret)
{
	int        i, ip_len, port;
	Peer_addr  *node = NULL, *p = peer_addr_head;

	if(peer_addr_head != NULL) 
	{
		printf("Must free peer_addr_head\n");
		return -1;
	}
	
	for(i = 0; i < ret; i++) 
	{
		if(memcmp(&buffer[i],"2:ip",4) == 0) 
		{
			i += 4;//跳过"2:ip"
			ip_len = 0;
			
			//获得IP的长度
			while(isdigit(buffer[i])) 
			{
				ip_len = ip_len * 10 + (buffer[i] - '0');
				i++;
			}
			i++;  // skip ":"
			node = (Peer_addr*)malloc(sizeof(Peer_addr));
			if(node == NULL) 
			{ 
				printf("%s:%d error",__FILE__,__LINE__); 
				continue;
			}
			
			//将IP存入链表中
			memcpy(node->ip,&buffer[i],ip_len);
			(node->ip)[ip_len] = '\0';
			node->next = NULL;
		}
		
		if(memcmp(&buffer[i],"4:port",6) == 0) 
		{
			i += 6;
			i++;  // skip "i"
			port = 0;
			while(isdigit(buffer[i])) 
			{
				port = port * 10 + (buffer[i] - '0');
				i++;
			}
			if(node != NULL)  
				node->port = port;
			else 
				continue;
			
			printf("+++ add a peer %-16s:%-5d +++ \n",node->ip,node->port);
			
			//感觉有问题
			if(p == peer_addr_head) 
			{ 
				peer_addr_head = node; 
				p = node; 
			}
			else 
				p->next = node;
			node = NULL;
		}
	}
	
	return 0;
}


/*
*功能:为已建立连接的peer创建peer结点并加入到peer链表中
*传入参数：
*传出参数：
*返回值：
*/

int add_peer_node_to_peerlist(int *sock,struct sockaddr_in saptr)
{
	Peer *node;
	
	node = add_peer_node();
	if(node == NULL)  
		return -1;
	
	node->socket = *sock;
	node->port   = ntohs(saptr.sin_port);
	node->state  = INITIAL;//处于初始化状态
	strcpy(node->ip,inet_ntoa(saptr.sin_addr));
	node->start_timestamp = time(NULL);//保存最近一次给Peer发送消息的时间

	return 0;
}

//释放Peer_addr结构堆空间
void free_peer_addr_head()
{
	Peer_addr *p = peer_addr_head;
  while(p != NULL) 
  {
		p = p->next;
		free(peer_addr_head);
		peer_addr_head = p;
  }
	peer_addr_head = NULL;
}
