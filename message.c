#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include "parse_metafile.h"
#include "bitfield.h"
#include "peer.h"
#include "data.h"
#include "policy.h"
#include "message.h"

#define HANDSHAKE   -2					//握手消息
#define KEEP_ALIVE  -1					//keep_alive消息
#define CHOKE        0					//chock消息
#define UNCHOKE      1					//uchock消息
#define INTERESTED   2					//interested消息
#define UNINTERESTED 3					//uninterested消息
#define HAVE         4					//have消息
#define BITFIELD     5					//bitfield消息
#define REQUEST      6					//request消息
#define PIECE        7					//piece消息
#define CANCEL       8					//cancel消息	
#define PORT         9					//port消息

// 如果45秒未给某peer发送消息，则发送keep_alive消息
#define KEEP_ALIVE_TIME 45

extern Bitmap *bitmap;								// 在bitmap.c中定义，指向己方的位图
extern char    info_hash[20];					// 在parse_metafile.c中定义，存放info_hash
extern char    peer_id[20];						// 在parse_metafile.c中定义，存放peer_id
extern int     have_piece_index[64];	// 在data.c中定义，存放下载到的piece的index
extern Peer   *peer_head;							// 在peer.c中定义，指向peer链表


/*
*功能:将int型的各个字节放到char型数组里
*传入参数：i的各个字节，
*传出参数：字符数组c
*返回值：
*	0	保存成功
*/
int int_to_char(int i, unsigned char c[4])
{
	c[3] = i%256;
	c[2] = (i-c[3])/256%256;
	c[1] = (i-c[3]-c[2]*256)/256/256%256;
	c[0] = (i-c[3]-c[2]*256-c[1]*256*256)/256/256/256%256;

	return 0;
}

/*
*功能:将char型数组放到int型中
*传入参数：字符数组c
*传出参数：
*返回值：
*	i	转换结果
*/
int char_to_int(unsigned char c[4])
{
	int i;

	i = c[0]*256*256*256 + c[1]*256*256 + c[2]*256 + c[3];
	
	return i;
}

/*
*功能：创建握手消息
*传入参数：
*	info_hash在parse_metafile.c中由种子文件计算而得
*	peer_id也在parse_metafile.c中生成
*	peer为要发送握手消息给某一个peer的指针变量
* 返回：
*	创建消息成功返回0,创建失败返回-1
*/
int create_handshake_msg(char *info_hash,char *peer_id,Peer *peer)
{
	int            i;
	unsigned char  keyword[20] = "BitTorrent protocol", c = 0x00;
	
	//发送缓存区大小16K(slice)+2K(其他消息)
	//跳过slice在缓冲区后追加其他消息
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;

	if(len < 68)  
		return -1;  // 68为握手消息的固定长度

	buffer[0] = 19;
	for(i = 0; i < 19; i++)  buffer[i+1]  = keyword[i];
	for(i = 0; i < 8;  i++)  buffer[i+20] = c;
	for(i = 0; i < 20; i++)  buffer[i+28] = info_hash[i];
	for(i = 0; i < 20; i++)  buffer[i+48] = peer_id[i];

	peer->msg_len += 68;
	return 0;
}

/*
*功能:创建keep_alive消息
*传入参数：Peer
*传出参数：
*返回值：
*	1	成功
*/
int create_keep_alive_msg(Peer *peer)
{
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;

	if(len < 4)  
		return -1;  // 4为keep_alive消息的固定长度

	memset(buffer,0,4);
	peer->msg_len += 4;
	return 0;
}


/*
*功能:创建chock消息
*传入参数：type peer
*传出参数：
*返回值：
*	0	保存成功
*/
int create_chock_interested_msg(int type,Peer *peer)
{
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;

	// 5为choke、unchoke、interested、uninterested消息的固定长度
	if(len < 5)  return -1;

	memset(buffer,0,5);
	buffer[3] = 1;
	buffer[4] = type;

	peer->msg_len += 5;
	return 0;
}

/*
*功能:创建have消息
*传入参数：index peer
*传出参数：
*返回值：
*	0	创建成功
*/
int create_have_msg(int index,Peer *peer)
{
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;
	unsigned char  c[4];

	if(len < 9)  return -1;  // 9为have消息的固定长度
	
	memset(buffer,0,9);	
	buffer[3] = 5;
	buffer[4] = 4;
	
	int_to_char(index,c);	// index为piece的下标
	buffer[5] = c[0];
	buffer[6] = c[1];
	buffer[7] = c[2];
	buffer[8] = c[3];
	
	peer->msg_len += 9;
	return 0;
}

/*
*功能:创建bitfield消息
*传入参数：bitfield bifield_len Peer
*传出参数：
*返回值：
*	0	创建成功
*/
int create_bitfield_msg(char *bitfield,int bitfield_len,Peer *peer)
{
	int            i;
	unsigned char  c[4];
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;

	if( len < bitfield_len+5 )  {  // bitfield消息的长度为bitfield_len+5
		printf("%s:%d buffer too small\n",__FILE__,__LINE__); 
		return -1;
	}

	int_to_char(bitfield_len+1,c);
	//组装位图消息
	for(i = 0; i < 4; i++)  
		buffer[i] = c[i];
		
	buffer[4] = 5;
	创建bitfield消息
	for(i = 0; i < bitfield_len; i++) 
		buffer[i+5] = bitfield[i];

	peer->msg_len += bitfield_len+5;  
	
	return 0;
}

/*
*功能：创建数据请求消息
*传入参数：
					index为请求的piece的下标
					begin为piece内的偏移量
					length为请求数据的长度
*返回：0 创建成功
*/
int create_request_msg(int index,int begin,int length,Peer *peer)
{
	int            i;
	unsigned char  c[4];
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;

	if(len < 17)  return -1;  // 17为request消息的固定长度

	memset(buffer,0,17);
	buffer[3] = 13;
	buffer[4] = 6;
	int_to_char(index,c);
	for(i = 0; i < 4; i++)  buffer[i+5]  = c[i];
	int_to_char(begin,c);
	for(i = 0; i < 4; i++)  buffer[i+9]  = c[i];
	int_to_char(length,c);
	for(i = 0; i < 4; i++)  buffer[i+13] = c[i];

	peer->msg_len += 17;
	
	return 0;
}

/*
*功能：创建piece消息
*传入参数：
					block指向待发送的数据
					b_len为block所指向的数据的长度
*返回： 0 创建成功
* 		-1 创建失败
*/
int create_piece_msg(int index,int begin,char *block,int b_len,Peer *peer)
{
	int            i;
	unsigned char  c[4];
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;

	if( len < b_len+13 ) {  // piece消息的长度为b_len+13
		printf("IP:%s len:%d\n",peer->ip,len);
		printf("%s:%d buffer too small\n",__FILE__,__LINE__); 
		return -1;
	}
	
	int_to_char(b_len+9,c);
	for(i = 0; i < 4; i++)     
	 buffer[i]    = c[i];
	 
	buffer[4] = 7;
	
	int_to_char(index,c);
	for(i = 0; i < 4; i++)      
		buffer[i+5]  = c[i];
	
	int_to_char(begin,c);
	for(i = 0; i < 4; i++)      
		buffer[i+9]  = c[i];
		
	for(i = 0; i < b_len; i++)  
		buffer[i+13] = block[i];

	peer->msg_len += b_len+13;
	  
	return 0;
}


/*
*功能:创建cancel消息
*传入参数：index bdegin length peer
*传出参数：
*返回值：
*	0	创建成功
*/
int create_cancel_msg(int index,int begin,int length,Peer *peer)
{
	int            i;
	unsigned char  c[4];
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;
	
	if(len < 17)  return -1;  // 17为cancel消息的固定长度
	
	memset(buffer,0,17);
	buffer[3] = 13;
	buffer[4] = 8;
	
	int_to_char(index,c);
	for(i = 0; i < 4; i++)  
		buffer[i+5]  = c[i];
	
	int_to_char(begin,c);
	for(i = 0; i < 4; i++)  
		buffer[i+9]  = c[i];
		
	int_to_char(length,c);
	for(i = 0; i < 4; i++)  
		buffer[i+13] = c[i];

	peer->msg_len += 17;	
	
	return 0;
}


/*
*功能:创建Port消息
*传入参数： port peer
*传出参数：
*返回值：
*	0	创建成功
*/
int create_port_msg(int port,Peer *peer)
{
	unsigned char  c[4];
	unsigned char  *buffer = peer->out_msg + peer->msg_len;
	int            len = MSG_SIZE - peer->msg_len;

	if( len < 7)  return 0;  // 7为port消息的固定长度

	memset(buffer,0,7);
	buffer[3] = 3;
	buffer[4] = 9;
	int_to_char(port,c);
	buffer[5] = c[2];
	buffer[6] = c[3];

	peer->msg_len += 7;
	
	return 0;
}

/*
*功能:以十六进制的形式打印消息的内容,用于调试
*传入参数：buffer len
*传出参数：
*返回值：
*	0	创建成功
*/
int print_msg_buffer(unsigned char *buffer, int len)
{
	int i;

	for(i = 0; i < len; i++) 
	{
		printf("%.2x ",buffer[i]);
		if( (i+1) % 16 == 0 )  
			printf("\n");
	}
	printf("\n");

	return 0;
}

/*
*功能:判断缓冲区中是否存放了一条完整的消息
*传入参数：buff len ok_len
*传出参数：
*返回值：
*	1	是
*	-1  否
*/
int is_complete_message(unsigned char *buff,unsigned int len,int *ok_len)
{
	unsigned int   i;
	char           btkeyword[20];

	unsigned char  keep_alive[4]   = { 0x0, 0x0, 0x0, 0x0 };
	unsigned char  chocke[5]       = { 0x0, 0x0, 0x0, 0x1, 0x0};
	unsigned char  unchocke[5]     = { 0x0, 0x0, 0x0, 0x1, 0x1};
	unsigned char  interested[5]   = { 0x0, 0x0, 0x0, 0x1, 0x2};
	unsigned char  uninterested[5] = { 0x0, 0x0, 0x0, 0x1, 0x3};
	unsigned char  have[5]         = { 0x0, 0x0, 0x0, 0x5, 0x4};
	unsigned char  request[5]      = { 0x0, 0x0, 0x0, 0xd, 0x6};
	unsigned char  cancel[5]       = { 0x0, 0x0, 0x0, 0xd, 0x8};
	unsigned char  port[5]         = { 0x0, 0x0, 0x0, 0x3, 0x9};
	
	if(buff==NULL || len<=0 || ok_len==NULL)  
		return -1;
		
	*ok_len = 0;
	
	btkeyword[0] = 19;
	memcpy(&btkeyword[1],"BitTorrent protocol",19);  // BitTorrent协议关键字

	unsigned char  c[4];
	unsigned int   length;
	
	for(i = 0; i < len; ) 
	{
		// 握手、chocke、have等消息的长度是固定的
		if( i+68<=len && memcmp(&buff[i],btkeyword,20)==0 )         i += 68;
		else if( i+4 <=len && memcmp(&buff[i],keep_alive,4)==0 )    i += 4;
		else if( i+5 <=len && memcmp(&buff[i],chocke,5)==0 )        i += 5;
		else if( i+5 <=len && memcmp(&buff[i],unchocke,5)==0 )      i += 5;
		else if( i+5 <=len && memcmp(&buff[i],interested,5)==0 )    i += 5;
		else if( i+5 <=len && memcmp(&buff[i],uninterested,5)==0 )  i += 5;
		else if( i+9 <=len && memcmp(&buff[i],have,5)==0 )          i += 9;
		else if( i+17<=len && memcmp(&buff[i],request,5)==0 )       i += 17;
		else if( i+17<=len && memcmp(&buff[i],cancel,5)==0 )        i += 17;
		else if( i+7 <=len && memcmp(&buff[i],port,5)==0 )          i += 7;
		// bitfield消息的长度是变化的
		else if( i+5 <=len && buff[i+4]==5 )  
		{
			c[0] = buff[i];   
			c[1] = buff[i+1];
			c[2] = buff[i+2]; 
			c[3] = buff[i+3];
			
			length = char_to_int(c);	
			
			// 消息长度占4字节,消息本身占length个字节
			if( i+4+length <= len )  
				i += 4+length;
			else 
			{ 
				*ok_len = i; 
				
				return -1; 
			}
		}
		// piece消息的长度也是变化的
		else if( i+5 <=len && buff[i+4]==7 )  
		{
			c[0] = buff[i];  
			c[1] = buff[i+1];
			c[2] = buff[i+2]; 
			c[3] = buff[i+3];
			
			length = char_to_int(c);
			// 消息长度占4字节,消息本身占length个字节
			if( i+4+length <= len )  
				i += 4+length;
			else 
			{ 
				*ok_len = i; 
				return -1; 
			}
		}
		else 
		{
			// 处理未知类型的消息
			if(i+4 <= len) 
			{
				c[0] = buff[i];   
				c[1] = buff[i+1];
				c[2] = buff[i+2]; 
				c[3] = buff[i+3];
				length = char_to_int(c);
				
				// 消息长度占4字节,消息本身占length个字节
				if(i+4+length <= len)  
				{ 
					i += 4+length; 
					continue; 
				}
				else 
				{ 
					*ok_len = i; 
					return -1; 
				}
				
			}
			// 如果也不是未知消息类型,则认为目前接收的数据还不是一个完整的消息
			*ok_len = i;
			return -1;
		}
	}
	
	*ok_len = i;
	return 1;
}

/*
*功能：

*/
/*
*功能:处理接收到的一条握手消息
*传入参数：
					从peer接收到这条握手消息
					buff指向握手消息
					len为buff的长度
*传出参数：
*返回值：
*	0  
*/
int process_handshake_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)  return -1;

	if(memcmp(info_hash,buff+28,20) != 0) 
	{ 
		peer->state = CLOSING;//表明处于即将与Peer断开状态
		// 丢弃发送缓冲区中的数据
		discard_send_buffer(peer);
		clear_btcache_before_peer_close(peer);
		close(peer->socket);
		return -1;
	}
	
	//获得Peer的ID
	memcpy(peer->id,buff+48,20);
	(peer->id)[20] = '\0';
	
	// 若当前处于Initial状态，则发送握手消息给peer
	if(peer->state == INITIAL) 
	{
		peer->state = HANDSHAKED;//表明处于全握手状态
		create_handshake_msg(info_hash,peer_id,peer);
	}
	
	// 若握手消息已发送，则状态转换为已握手状态
	if(peer->state == HALFSHAKED)  
		peer->state = HANDSHAKED;

	// 记录最近收到该peer消息的时间
	// 若一定时间内(如两分钟)未收到来自该peer的任何消息，则关闭连接
	peer->start_timestamp = time(NULL);
	return 0;
}

/*
*功能:处理刚刚接收到的来自peer的keepv_alive消息
*传入参数：peer  buff len
*传出参数：
*返回值：
*	0  处理成功
*/
int process_keep_alive_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)  return -1;

	peer->start_timestamp = time(NULL);
	return 0;
}

/*
*功能:处理收到的choke消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*   -1  没有
*/
int process_choke_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)  return -1;

	if( peer->state!=CLOSING && peer->peer_choking==0 ) 
	{
		peer->peer_choking = 1;//被Peer阻塞
		peer->last_down_timestamp = 0;//最近下载数据开始时间
		peer->down_count          = 0;//下载的字节数
		peer->down_rate           = 0;//下载速度
	}

	peer->start_timestamp = time(NULL);//最近一次接收Peer消息的时间
	
	return 0;
}

/*
*功能:处理unchoke消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*/
int process_unchoke_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)  return -1;

	// 若原来处于choke状态且与该peer的连接未被关闭
	if( peer->state!=CLOSING && peer->peer_choking==1 ) 
	{
		peer->peer_choking = 0;//Peer解除对客服端的阻塞
		//客服端对Peer感兴趣，则构造request消息请求peer发送数据
		if(peer->am_interested == 1)  
			create_req_slice_msg(peer);
		else 
		{
			peer->am_interested = is_interested(&(peer->bitmap), bitmap);
			if(peer->am_interested == 1) 
				create_req_slice_msg(peer);
			else 
				printf("Received unchoke but Not interested to IP:%s \n",peer->ip);
		}

		peer->last_down_timestamp = 0;
		peer->down_count          = 0;
		peer->down_rate           = 0;
	}

	peer->start_timestamp = time(NULL);//最近一次接收Peer消息的时间
	
	return 0;
}

/*
*功能:处理收到的interested消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*/
int process_interested_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)  return -1;

	//客户端与peer的连接未被关闭且交换数据状态
	if( peer->state!=CLOSING && peer->state==DATA ) 
	{
		peer->peer_interested = is_interested(bitmap, &(peer->bitmap));
		if(peer->peer_interested == 0)  
			return -1;
		if(peer->am_choking == 0) 
			create_chock_interested_msg(1,peer);//创建非阻塞消息
	}

	peer->start_timestamp = time(NULL);
	
	return 0;
}

/*
*功能:处理收到的uninterested消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*	-1	出错
*/
int process_uninterested_msg(Peer *peer,unsigned char *buff,int len)
{
	if(peer==NULL || buff==NULL)  
		return -1;

	if( peer->state!=CLOSING && peer->state==DATA ) 
	{
		peer->peer_interested = 0;//Peer对客服端不感兴趣
		cancel_requested_list(peer);//撤销请求队列
	}

	peer->start_timestamp = time(NULL);
	
	return 0;
}

/*
*功能:处理have消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*/
int process_have_msg(Peer *peer,unsigned char *buff,int len)
{
	int           rand_num;
	unsigned char c[4];

	if(peer==NULL || buff==NULL)  return -1;

	srand(time(NULL));
	rand_num = rand() % 3;

	if( peer->state!=CLOSING && peer->state==DATA ) 
	{
		c[0] = buff[5]; 
		c[1] = buff[6];
		c[2] = buff[7]; 
		c[3] = buff[8];		
		
		if(peer->bitmap.bitfield != NULL)
			set_bit_value(&(peer->bitmap),char_to_int(c),1);//更新位图

		if(peer->am_interested == 0) 
		{
			peer->am_interested = is_interested(&(peer->bitmap), bitmap);
			// 由原来的对peer不感兴趣变为感兴趣时,发interested消息
			if(peer->am_interested == 1) 
				create_chock_interested_msg(2,peer);	
		} 
		else 
		{  // 收到三个have则发一个interested消息
			if(rand_num == 0) 
				create_chock_interested_msg(2,peer);
		}
	}

	peer->start_timestamp = time(NULL);
	
	return 0;
}

/*
*功能:处理cancel消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*	-1  出错
*/
int process_cancel_msg(Peer *peer,unsigned char *buff,int len)
{
	unsigned char c[4];
	int           index, begin, length;

	if(peer==NULL || buff==NULL)  
		return -1;
	
	c[0] = buff[5];  
	c[1] = buff[6];
	c[2] = buff[7];  
	c[3] = buff[8];
	index = char_to_int(c);//获得index
	
	c[0] = buff[9];  
	c[1] = buff[10];
	c[2] = buff[11]; 
	c[3] = buff[12];
	begin = char_to_int(c);//获得begin
	
	c[0] = buff[13]; 
	c[1] = buff[14];
	c[2] = buff[15]; 
	c[3] = buff[16];
	length = char_to_int(c);//获得Length
	
	// 在被请求队列中删除指定的请求
	Request_piece *p, *q;
	p = q = peer->Requested_piece_head;//取得Peer请求队列头指针
	while(p != NULL) 
	{ 
		if( p->index==index && p->begin==begin && p->length==length ) 
		{
			if(p == peer->Requested_piece_head) 
				peer->Requested_piece_head = p->next;
			else
				q->next = p->next;
			free(p);
			break;
		}
		q = p;
		p = p->next;
	}	

	peer->start_timestamp = time(NULL);
	return 0;
}

/*
*功能:处理收到的位图消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*	-1  出错
*/
int process_bitfield_msg(Peer *peer,unsigned char *buff,int len)
{
	unsigned char c[4];

	if(peer==NULL || buff==NULL)  return -1;
	//握手和已发送位图状态
	if(peer->state==HANDSHAKED || peer->state==SENDBITFIELD) 
	{
		c[0] = buff[0];   
		c[1] = buff[1];
		c[2] = buff[2];   
		c[3] = buff[3];			
		
		// 若原先已收到一个位图消息，则清空原来的位图
		if( peer->bitmap.bitfield != NULL ) 
		{
			free(peer->bitmap.bitfield);
			peer->bitmap.bitfield = NULL;
		}
		
		peer->bitmap.valid_length = bitmap->valid_length;
		
		// 若收到的一个错误位图
		if(bitmap->bitfield_length != char_to_int(c)-1) 
		{
			peer->state = CLOSING;
			// 丢弃发送缓冲区中的数据
			discard_send_buffer(peer);
			clear_btcache_before_peer_close(peer);
			close(peer->socket);
			return -1;
		}
		// 生成该peer的位图
		peer->bitmap.bitfield_length = char_to_int(c) - 1;
		peer->bitmap.bitfield = (unsigned char *)malloc(peer->bitmap.bitfield_length);
		memcpy(peer->bitmap.bitfield,&buff[5],peer->bitmap.bitfield_length);
	
		// 如果原状态为已握手,收到位图后应该向peer发位图
		if(peer->state == HANDSHAKED) 
		{
			create_bitfield_msg(bitmap->bitfield,bitmap->bitfield_length,peer);
			peer->state = DATA;
		}
		// 如果原状态为已发送位图，收到位图后可以准备交换数据
		if(peer->state == SENDBITFIELD) 
		{
			peer->state = DATA;
		}

		// 判断peer是否对我们感兴趣
		peer->peer_interested = is_interested(bitmap,&(peer->bitmap));
		// 判断对peer是否感兴趣,若是则发送interested消息
		peer->am_interested = is_interested(&(peer->bitmap), bitmap);
		if(peer->am_interested == 1) 
			create_chock_interested_msg(2,peer);
	}
	
	peer->start_timestamp = time(NULL);
	
	return 0;
}

/*
*功能：处理收到的request消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*	-1	出错
*/
int process_request_msg(Peer *peer,unsigned char *buff,int len)
{
	unsigned char  c[4];
	int            index, begin, length;
	Request_piece  *request_piece, *p;
	
	if(peer==NULL || buff==NULL)  return -1;

	if(peer->am_choking==0 && peer->peer_interested==1) 
	{
		c[0] = buff[5];  c[1] = buff[6];
		c[2] = buff[7];  c[3] = buff[8];
		index = char_to_int(c);
		c[0] = buff[9];  c[1] = buff[10];
		c[2] = buff[11]; c[3] = buff[12];
		begin = char_to_int(c);
		c[0] = buff[13]; c[1] = buff[14];
		c[2] = buff[15]; c[3] = buff[16];
		length = char_to_int(c);

		// 错误的slice请求
		if( begin%(16*1024) != 0 ) 
		{
			return 0;
		}
		
		// 查看该请求是否已存在,若已存在,则不进行处理
		p = peer->Requested_piece_head;
		while(p != NULL) 
		{
			if(p->index==index && p->begin==begin && p->length==length) 
			{
				break;
			}
			p = p->next;
		}
		if(p != NULL)  //请求存在
			return 0;

		// 将请求加入到请求队列中
		request_piece = (Request_piece *)malloc(sizeof(Request_piece));
		if(request_piece == NULL)  
		{ 
			printf("%s:%d error",__FILE__,__LINE__); 
			return 0; 
		}
		request_piece->index  = index;
		request_piece->begin  = begin;
		request_piece->length = length;
		request_piece->next   = NULL;
		
		if( peer->Requested_piece_head == NULL ) 
			peer->Requested_piece_head = request_piece;
		else 
		{
			p = peer->Requested_piece_head;
			while(p->next != NULL)  
				p = p->next;
			p->next = request_piece;
		}
		//printf("*** add a request FROM IP:%s index:%-6d begin:%-6x ***\n",
		//       peer->ip,index,begin);
	}

	peer->start_timestamp = time(NULL);
	
	return 0;
}

/*
*功能：处理收到的piece消息
*传入参数：peer buff len
*传出参数：
*返回值：
*	0   处理成功
*	-1  出错
*/
int process_piece_msg(Peer *peer,unsigned char *buff,int len)
{
	unsigned char  c[4];
	int            index, begin, length;
	Request_piece  *p;

	if(peer==NULL || buff==NULL)  
		return -1;
	
	if(peer->peer_choking==0) 
	{
		c[0] = buff[0];    c[1] = buff[1];
		c[2] = buff[2];    c[3] = buff[3];
		length = char_to_int(c) - 9;
		c[0] = buff[5];    c[1] = buff[6];
		c[2] = buff[7];    c[3] = buff[8];
		index = char_to_int(c);
		c[0] = buff[9];    c[1] = buff[10];
		c[2] = buff[11];   c[3] = buff[12];
		begin = char_to_int(c);

		//查看请求是否存在
		p = peer->Request_piece_head;
		while(p != NULL) 
		{
			if(p->index==index && p->begin==begin && p->length==length)
				break;
			p = p->next;
		}
		if(p == NULL) 
		{
			printf("did not found matched request\n"); 
			return -1;
		}

		if(peer->last_down_timestamp == 0)
			peer->last_down_timestamp = time(NULL);
		peer->down_count += length;
		peer->down_total += length;

		//将slice写入缓存区
		write_slice_to_btcache(index,begin,length,buff+13,length,peer);

		create_req_slice_msg(peer);
	}

	peer->start_timestamp = time(NULL);
	return 0;
}


/*
*功能:处理收到的消息（peer的接收缓冲区中可能存放着多条消息）
*传入参数：peer 
*传出参数：
*返回值：
*	0   处理成功
*	-1 	出错
*/
int parse_response(Peer *peer)
{
	unsigned char  btkeyword[20];
	unsigned char  keep_alive[4] = { 0x0, 0x0, 0x0, 0x0 };
	int            index;
	unsigned char  *buff = peer->in_buff;//获取接收到的消息
	int            len = peer->buff_len;//缓冲区的长度

	if(buff==NULL || peer==NULL)  
		return -1;

	btkeyword[0] = 19;
	memcpy(&btkeyword[1],"BitTorrent protocol",19);  // BitTorrent协议关键字

	// 分别处理12种消息
	for(index = 0; index < len; ) 
	{	

		if( (len-index >= 68) && (memcmp(&buff[index],btkeyword,20) == 0) ) 
		{
			process_handshake_msg(peer,buff+index,68);
			index += 68;
		} 
		else if( (len-index >= 4) && (memcmp(&buff[index],keep_alive,4) == 0))
		{
			process_keep_alive_msg(peer,buff+index,4);
			index += 4; 
		}
		else if( (len-index >= 5) && (buff[index+4] == CHOKE) ) 
		{
			process_choke_msg(peer,buff+index,5);
			index += 5;
		}
		else if( (len-index >= 5) && (buff[index+4] == UNCHOKE) ) 
		{
			process_unchoke_msg(peer,buff+index,5);
			index += 5;
		}
		else if( (len-index >= 5) && (buff[index+4] == INTERESTED) ) 
		{
			process_interested_msg(peer,buff+index,5);
			index += 5;
		}
		else if( (len-index >= 5) && (buff[index+4] == UNINTERESTED) ) 
		{
			process_uninterested_msg(peer,buff+index,5);
			index += 5;
		}
		else if( (len-index >= 9) && (buff[index+4] == HAVE) ) 
		{
			process_have_msg(peer,buff+index,9);
			index += 9;
		}
		else if( (len-index >= 5) && (buff[index+4] == BITFIELD) ) 
		{
			process_bitfield_msg(peer,buff+index,peer->bitmap.bitfield_length+5);
			index += peer->bitmap.bitfield_length + 5;
		}
		else if( (len-index >= 17) && (buff[index+4] == REQUEST) ) 
		{
			process_request_msg(peer,buff+index,17);
			index += 17;
		}
		else if( (len-index >= 13) && (buff[index+4] == PIECE) ) 
		{
			unsigned char  c[4];
			int            length;
			
			c[0] = buff[index];    c[1] = buff[index+1];
			c[2] = buff[index+2];  c[3] = buff[index+3];
			length = char_to_int(c) - 9;
			
			process_piece_msg(peer,buff+index,length+13);
			index += length + 13; // length+13为piece消息的长度
		}
		else if( (len-index >= 17) && (buff[index+4] == CANCEL) ) 
		{
			process_cancel_msg(peer,buff+index,17);
			index += 17;
		}
		else if( (len-index >= 7) && (buff[index+4] == PORT) ) 
		{
			index += 7;
		}
		else 
		{
			// 如果是未知的消息类型,则跳过不予处理
			unsigned char c[4];
			int           length;
			if(index+4 <= len) 
			{
				c[0] = buff[index];   c[1] = buff[index+1];
				c[2] = buff[index+2]; c[3] = buff[index+3];
				length = char_to_int(c);
				if(index+4+length <= len)  
				{ 
					index += 4+length; 
					continue; 
				}
			}
			// 如果是一条错误的消息,清空接收缓冲区
			peer->buff_len = 0;
			
			return -1;
		}
	} // end for

	// 接收缓冲区中的消息处理完毕后,清空接收缓冲区
	peer->buff_len = 0;

	return 0;
}

/*
*功能:处理收到的不完整的消息
*传入参数：
					ok_len为接收缓冲区中完整消息的长度
*传出参数：
*返回值：
*	0   处理成功
*	-1	出错
*/
int parse_response_uncomplete_msg(Peer *p,int ok_len)
{
	char *tmp_buff;
	int   tmp_buff_len;

	// 分配存储空间,并保存接收缓冲区中不完整的消息
	tmp_buff_len = p->buff_len - ok_len;
	if(tmp_buff_len <= 0)  
		return -1;
	tmp_buff = (char *)malloc(tmp_buff_len);
	if(tmp_buff == NULL) 
	{
		printf("%s:%d error\n",__FILE__,__LINE__);
		return -1;
	}
	memcpy(tmp_buff,p->in_buff+ok_len,tmp_buff_len);
	
	// 处理接收缓冲区中前面完整的消息
	p->buff_len = ok_len;
	parse_response(p);

	// 将不完整的消息拷贝到接收缓冲区的开始处
	memcpy(p->in_buff,tmp_buff,tmp_buff_len);
	p->buff_len = tmp_buff_len;
	if(tmp_buff != NULL)  
		free(tmp_buff);

	return 0;
}

/*
*功能:当下载完一个piece时,应该向所有的peer发送have消息
*传入参数：
*传出参数：
*返回值：
*	0   处理成功
*	-1	出错
*/
int prepare_send_have_msg()
{
	Peer *p = peer_head;//获得当前与客服端通信的Peer链表头指针
	int  i;

	if(peer_head == NULL)  
		return -1;
	if(have_piece_index[0] == -1)  
		return -1;

	while(p != NULL) 
	{
		for(i = 0; i < 64; i++) 
		{
			if(have_piece_index[i] != -1) 
			{
				create_have_msg(have_piece_index[i],p);
			}
			else 
			{
				break;
			}
		}
		p = p->next;
	}

	for(i = 0; i < 64; i++) 
	{
		if(have_piece_index[i] == -1) 
		{
			break;
		}
		else 
		{
			have_piece_index[i] = -1;
		}
	}
	
	return 0;
}

/*
*功能:主动创建发送给peer的消息,而不是等收到某个消息再作出响应
*传入参数：peer 
*传出参数：
*返回值：
*	0   处理成功
*/
int create_response_message(Peer *peer)
{
	if(peer==NULL)  return -1;

	if(peer->state == INITIAL) 
	{
		//创建握手消息
		create_handshake_msg(info_hash,peer_id,peer);
		peer->state = HALFSHAKED;
		return 0;
	}

	if(peer->state == HANDSHAKED) 
	{
		if(bitmap == NULL)  
			return -1;
		//创建位图消息
		create_bitfield_msg(bitmap->bitfield,bitmap->bitfield_length,peer);
		peer->state = SENDBITFIELD;
		return 0;
	}

	// 发送piece消息,即发送下载文件的内容
	if( peer->am_choking==0 && peer->Requested_piece_head!=NULL ) 
	{
		Request_piece *req_p = peer->Requested_piece_head;
		//从缓存区获取slice消息
		int ret = read_slice_for_send(req_p->index,req_p->begin,req_p->length,peer);
		if(ret < 0 )
		{ 
			printf("read_slice_for_send ERROR\n");
		}
		else
		{
			//更新Peer结构体中的信息
			if(peer->last_up_timestamp == 0) 
				peer->last_up_timestamp = time(NULL);
			peer->up_count += req_p->length;
			peer->up_total += req_p->length;
			peer->Requested_piece_head = req_p->next;

			//printf("********* sending a slice TO:%s index:%-5d begin:%-5x *********\n",
			//peer->ip,req_p->index,req_p->begin);

			free(req_p);
			return 0;
		}
	}

	// 如果3分钟没有收到任何消息关闭连接
	time_t now = time(NULL);  // 获取当前时间
	long interval1 = now - peer->start_timestamp;
	if( interval1 > 180 ) 
	{
		peer->state = CLOSING;
		discard_send_buffer(peer);  // 丢弃发送缓冲区中的数据
		clear_btcache_before_peer_close(peer);
		close(peer->socket);
	}
	// 如果45秒没有发送和接收消息,则发送一个keep_alive消息
	long interval2 = now - peer->recet_timestamp;
	if( interval1>45 && interval2>45 && peer->msg_len==0)
		create_keep_alive_msg(peer);

	return 0;
}

/*
*功能:即将与peer断开时,丢弃发送缓冲区中的消息
*传入参数：peer
*传出参数：
*返回值：
*/
void discard_send_buffer(Peer *peer)
{
	struct linger  lin;
	int            lin_len;
	
	lin.l_onoff  = 1;
	lin.l_linger = 0;
	lin_len      = sizeof(lin);
	
	if(peer->socket > 0)
	{
		setsockopt(peer->socket,SOL_SOCKET,SO_LINGER,(char *)&lin,lin_len);
	}
}
