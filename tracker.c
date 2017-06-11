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
*���ܣ�����HTTPЭ����б���ת��
*���������
*	*in 
*	len1
*����������
*	*out
*	len2
*����ֵ��
* -1	����
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
		{//Ӣ���ַ�
			out[j] = in[i];
		}
		else 
		{ 
			out[j] = '%';
			j++;
			out[j] = hex_table[in[i] >> 4];//ȡ��ASCII��ĸ���λ
			j++;
			out[j] = hex_table[in[i] & 0xf];//ȡ��ASCII��ĵ���λ
		}
	}
	out[j] = '\0';
	
#ifdef DEBUG
	//printf("http encoded:%s\n",out);
#endif
	
	return 0;
}

/*
*���ܣ���ȡTracker URL�е�����������
*���������
*	*node ����Tracker��URL�����ڵ�
*	len
*����������
* *name��������
*����ֵ��
* -1 ����
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
	{//�Թ�"http://"
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
*���ܣ���ȡTracker URL�еĶ˿ں�
*���������
*	*node ����Tracker��URL�����ڵ�
*����������
* *port	�˿ں�
*����ֵ��
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
		
	*port = 0;//��ʼ��
	
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
*���ܣ����췢�͵�Tracker��������HTTP GET����
*���������
*						numwant��ϣ��Tracker���ص�Peer��
*						nodeΪָ��Tracker��URL
*						portΪ�����Ķ˿ں�
*						downΪ�����ص�������
*						upΪ���ϴ���������
*						leftΪʣ������ֽ�δ����
*����������
*						*request���ڽ������ɵ�����
*						lenΪrequest��ָ�������ĳ���
*����ֵ��
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

	//HTTPЭ�����ת��
	//�������ļ��е�hashֵ����HTTPЭ�����ת��
	//�����ֵpeer_id����HTTPЭ�����ת��
	http_encode(info_hash,20,encoded_info_hash,100);
	http_encode(peer_id,20,encoded_peer_id,100);

	//����һ�����������ʶ�ͷ���
	srand(time(NULL));
	key = rand() / 10000;

	//��ȡ�������Ͷ˿ں�
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

/*���ܣ���ȡTracker���ص���Ϣ
*���������
*					bufferָ��Tracker�Ļ�Ӧ��Ϣ
*					lenΪbuffer��ָ�������ĳ���
*����������
*					total_length���ڴ��Tracker�������ݵĳ���
*����ֵ��
* -1
* 0	��һ����Ϣ����
* 1
*/
int get_response_type(char *buffer,int len,int *total_length)
{
	int i, content_length = 0;

	//���ҹؼ���"5:peers"
	for(i = 0; i < len-7; i++) 
	{
		if(memcmp(&buffer[i],"5:peers",7) == 0) 
		{ 
			i = i+7;//����"5:peers"
			break; 
		}
	}
	if(i == len-7) 
	{		
		return -1;  // ���ص���Ϣ����"5:peers"�ؼ���
	}
		
	if(buffer[i] != 'l')  
	{
		return 0;   // ���ص���Ϣ������Ϊ��һ��
	}

	*total_length = 0;
	//���ҹؼ���"Content-Length: "
	for(i = 0; i < len-16; i++) 
	{
		if(memcmp(&buffer[i],"Content-Length: ",16) == 0) 
		{
			i = i+16;//����"Content-Length: "
			break; 
		}
	}
	
	if(i != len-16) 
	{
		while(isdigit(buffer[i])) 
		{
			//���Tracker������Ϣ�ĳ���
			content_length = content_length * 10 + (buffer[i] - '0');
			i++;
		}
		
		//���ҹؼ���"\r\n\r\n"
		for(i = 0; i < len-4; i++) 
		{
			if(memcmp(&buffer[i],"\r\n\r\n",4) == 0)  
			{ 
				i = i+4;//����"\r\n\r\n"
				break; 
			}
		}
		
		//���Tracker������Ϣ���ܳ���
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
hostent�ṹ˵�����£�
	struct  hostent
	{
	    char *h_name;           //��ʽ����������
	    char **h_aliases;       //ָ���������Ƶ���������
	    int h_addrtype;         //��ַ����̬�� ͨ����AF_INET 
	    int h_length;           //��ַ�ĳ���
	    char **h_addr_list;     //������������ȡ�ø����������е�ַ
	};
*/
/*
����ֵ ���ɹ�����hostent�ṹָ�룬ʧ���򷵻�NULLָ�룬 ����ԭ�����h_errno������
������룺
                HOST_NOT_FOUND                �Ҳ���ָ��������
                NO_ADDRESS                    ������������ȴ��IP��ַ
                NO_RECOVERY                   �����������д�����
                TRY_AGAIN                     ���ٵ���һ��
*���ܣ��Է������ķ�ʽ����Tracker
*���������
*����������
*����ֵ��
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
	
	tracker_count = count;//��ȡTracker��URL������
	//����洢socket�ļ��������Ķѿռ�
	sock = (int *)malloc(count * sizeof(int));
	
	if(sock == NULL)  
		goto OUT;
		
	//����洢sockaddr_in �ṹ��ѿռ�
	tracker = (struct sockaddr_in *)malloc(count * sizeof(struct sockaddr_in));
	
	if(tracker == NULL)  
		goto OUT;
		
	valid = (int *)malloc(count * sizeof(int));
	
	if(valid == NULL)  
		goto OUT;
	
	p = announce_list_head;//��ȡ����URL������ͷָ��
	for(i = 0; i < count; i++) 
	{
		char tracker_name[128];
		unsigned short tracker_port = 0;
		
		//����count��ͨ�Žڵ�
		sock[i] = socket(AF_INET,SOCK_STREAM,0);
		if(sock < 0) 
		{
			printf("%s:%d socket create failed\n",__FILE__,__LINE__);
			valid[i] = 0;
			p = p->next;
			continue;
		}

		//��ȡTracker��URL
		get_tracker_name(p,tracker_name,128);
		//��ȡ�˿ں�
		get_tracker_port(p,&tracker_port);
		
		// ����������ȡIP��ַ
		ht = gethostbyname(tracker_name);
		if(ht == NULL) 
		{
			printf("gethostbyname failed:%s\n",hstrerror(h_errno)); 
			valid[i] = 0;
		} 
		else 
		{
			//���sockaddr_in�ṹ��ռ�
			memset(&tracker[i], 0, sizeof(struct sockaddr_in));
			//����IP��ַ���˿ںš�Э���嵽sockaddr_in�ṹ����
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
			
			//��ȡ�ļ�����ģʽ
			flags = fcntl(sock[i],F_GETFL,0);
			// �����׽���Ϊ������
			fcntl(sock[i],F_SETFL,flags|O_NONBLOCK);
			
			// ����tracker
			ret = connect(sock[i],(struct sockaddr *)&tracker[i],
				          sizeof(struct sockaddr));
				          
			if(ret < 0 && errno != EINPROGRESS) 
			{				
				valid[i] = 0;	
			}
			// �������0��˵�������Ѿ�����
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
*���ܣ��Է������ķ�ʽ����peer
*���������
*����������
*����ֵ��
*/
int prepare_connect_peer(int *max_sockfd)
{
	int       i, flags, ret, count = 0;
	Peer_addr *p;
	
	p = peer_addr_head;//��ñ���PeerIP��ַ�Ͷ˿ںŵ�����ͷָ��
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
	
	p = peer_addr_head;  // �˴�p���¸�ֵ
	for(i = 0; i < count && p != NULL; i++) 
	{
		//����count��ͨ�Žڵ㲢����ļ�������
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
		// �����׽���Ϊ������
		flags = fcntl(peer_sock[i],F_GETFL,0);
		fcntl(peer_sock[i],F_SETFL,flags|O_NONBLOCK);
		// ����peer
		ret = connect(peer_sock[i],(struct sockaddr *)&peer_addr[i],
			          sizeof(struct sockaddr));
		if(ret < 0 && errno != EINPROGRESS)  
			peer_valid[i] = 0;
		// �������0��˵�������Ѿ�����
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
*����:������һ��Tracker�Ļ�Ӧ��Ϣ
*���������
*����������
*����ֵ��
*
*/
int parse_tracker_response1(char *buffer,int ret,char *redirection,int len)
{
	int           i, j, count = 0;
	unsigned char c[4];
	Peer_addr     *node, *p;

	//���ҹؼ���"Location: "
	for(i = 0; i < ret - 10; i++) 
	{
		if(memcmp(&buffer[i],"Location: ",10) == 0) 
		{ 
			i = i + 10;//����"Location: "
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

	// ��ȡ���ص�peer��,�ؼ���"5:peers"֮��Ϊ����Peer��IP�Ͷ˿�
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
	i++;  // ����":"

	//���IP��ַ�Ͷ˿ڵ�����
	count = (ret - i) / 6;
		
	// ��ÿ��peer��IP�Ͷ˿ڱ��浽peer_addr_headָ���������
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
	
		// �жϵ�ǰpeer�Ƿ��Ѿ�������������
		p = peer_addr_head;
		while(p != NULL) 
		{
			if( memcmp(node->ip,p->ip,strlen(node->ip)) == 0 ) 
			{ 
				//�Ѵ����ͷŵ�ǰ��㲻���д洢
				free(node); 
				break;
			}
			p = p->next;
		}
			
		// ����ǰ������ӵ�������
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
*����:�����ڶ���Tracker�Ļ�Ӧ��Ϣ
*���������
*����������
*����ֵ��
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
			i += 4;//����"2:ip"
			ip_len = 0;
			
			//���IP�ĳ���
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
			
			//��IP����������
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
			
			//�о�������
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
*����:Ϊ�ѽ������ӵ�peer����peer��㲢���뵽peer������
*���������
*����������
*����ֵ��
*/

int add_peer_node_to_peerlist(int *sock,struct sockaddr_in saptr)
{
	Peer *node;
	
	node = add_peer_node();
	if(node == NULL)  
		return -1;
	
	node->socket = *sock;
	node->port   = ntohs(saptr.sin_port);
	node->state  = INITIAL;//���ڳ�ʼ��״̬
	strcpy(node->ip,inet_ntoa(saptr.sin_addr));
	node->start_timestamp = time(NULL);//�������һ�θ�Peer������Ϣ��ʱ��

	return 0;
}

//�ͷ�Peer_addr�ṹ�ѿռ�
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