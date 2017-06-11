#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "parse_metafile.h"
#include "peer.h"
#include "data.h"
#include "message.h"
#include "policy.h"

Unchoke_peers  unchoke_peers;// 存放非阻塞Peer和优化非阻塞Peer的指针
long long      total_down = 0L, total_up = 0L;// 总的下载量和上传量
float          total_down_rate = 0.0F, total_up_rate = 0.0F;// 总的下载上传速度
int            total_peers = 0;// 已连接的总Peer数
 
extern int	   end_mode;				// 是否已进入终端模式
extern Bitmap  *bitmap;					// 指向己方的位图
extern Peer    *peer_head;			// 指向Peer链表
extern int     pieces_length;		// 所有piece hash值的长度
extern int     piece_length;		// 每个piece的长度

extern Btcache *btcache_head;		//指向一个维护16K缓冲区的结构体
extern int     last_piece_index;//最后一个piece号
extern int     last_piece_count;//最后一个piece的slice数量
extern int     last_slice_len;	//最后下载的piece中最后的slice的大小
extern int     download_piece_num;//当前已下载的piece数量

/*
*功能：初始化全局变量unchoke_peers
*传入参数：无
*传出参数：unchoke_peers
*返回值：
*/
void init_unchoke_peers()
{
	int i;

	for(i = 0; i < UNCHOKE_COUNT; i++) 
	{//非阻塞的peer的初始化
		*(unchoke_peers.unchkpeer + i) = NULL;//初始化保存非阻塞Peer的指针	
	}

	unchoke_peers.count = 0;	//当前非阻塞的peer的个数
	unchoke_peers.optunchkpeer = NULL;//初始化保存优化非阻塞Peer的指针
}

/*
*功能：判断一个peer是否已经存在于unchoke_peers
*传入参数：peer的地址
*传出参数：
*返回值：
		1	peer非阻塞
		0	peer阻塞
*/
int is_in_unchoke_peers(Peer *node)
{
	int i;

	for(i = 0; i < unchoke_peers.count; i++) 
	{
		if( node == (unchoke_peers.unchkpeer)[i] )  
		{//比较指针中的值和参数(地址)
			return 1;
		}
	}

	return 0;
}

/*
*功能：从unchoke_peers(指针数组)中获取下载速度最慢的peer的索引
*传入参数：peer的地址和对应的长度
*传出参数：
*返回值：
		-1   没有任何速度不存在此peer
		j	 速度
		
*/
int get_last_index(Peer **array,int len)
{
	int i, j = -1;

	if(len <= 0) 
	{
		return j;
	}
	else 
	{
		j = 0;
	}

	for(i = 0; i < len; i++)
	{
		if( array[i]->down_rate < array[j]->down_rate )  
		{
			j = i;
		}
	}

	return j;
}

/*
*功能：找出当前下载速度最快的4个peer,将其unchoke
*传入参数：
*传出参数：
*返回值：
*/
int select_unchoke_peer()
{
	Peer *p;
	Peer *now_fast[UNCHOKE_COUNT];// 若peer链表中下载速度最快的4个peer
	Peer *force_choke[UNCHOKE_COUNT];// 要将其强行阻塞的peer
	int unchoke_socket[UNCHOKE_COUNT];// 存放刚刚被解除阻塞的peer的socket
	int choke_socket[UNCHOKE_COUNT];// 存放刚刚被阻塞的peer的socket
	int i, j, index = 0, len = UNCHOKE_COUNT;

	//初始化
	for(i = 0; i < len; i++) 
	{//0~3
		now_fast[i] = NULL;
		force_choke[i] = NULL;
		unchoke_socket[i] = -1;
		choke_socket[i] = -1;
	}

	// 将那些在过去10秒已断开连接而又处于unchoke队列中的peer清除出unchoke队列
	for(i = 0, j = 0; i < unchoke_peers.count; i++) 
	{
		p = peer_head;
		//查找unchoke_peers结构体链表中的非阻塞Peer
		while(p != NULL) 
		{
			if(p == unchoke_peers.unchkpeer[i])  
				break;
				
			p = p->next;
		}
		
		//在peer结构体链表中不存在说明下载者已经断开连接
		//清除出非阻塞队列
		if(p == NULL)  
		{ 
			unchoke_peers.unchkpeer[i] = NULL; 
			j++; //若运行此处则j非0
		}
	}
	
	if(j != 0) 
	{
		//更新非阻塞Peer的数量
		unchoke_peers.count = unchoke_peers.count - j;//目前连接的个数
		for(i = 0, j = 0; i < len; i++) 
		{
			if(unchoke_peers.unchkpeer[i] != NULL) 
			{
				force_choke[j] = unchoke_peers.unchkpeer[i];
				j++;
			}
		}
		for(i = 0; i < len; i++) 
		{
			unchoke_peers.unchkpeer[i] = force_choke[i];
			force_choke[i] = NULL;
		}
	}

	// 将那些在过去10秒上传速度超过50KB/S而下载速度过小的peer强行阻塞
	// 注意：up_rate和down_rate的单位是B/S而不是KB/S
	for(i = 0, j = -1; i < unchoke_peers.count; i++) 
	{
		if( (unchoke_peers.unchkpeer)[i]->up_rate > 50*1024 &&
			(unchoke_peers.unchkpeer)[i]->down_rate < 0.1*1024 ) 
		{
			j++;
			//找出上传速度大于50KB下载速度低于0.1KB的Peer
			force_choke[j] = unchoke_peers.unchkpeer[i];
		}
	}

	// 从当前所有Peer中选出下载速度最快的四个peer,让下载速度最快的peer提供上传
	p = peer_head;
	while(p != NULL) 
	{
		if(p->state==DATA && is_interested(bitmap,&(p->bitmap)) && is_seed(p)!=1) 
		{
			// p不应该在force_choke数组中
			for(i = 0; i < len; i++) 
			{
				if(p == force_choke[i]) 
					break;
			}
			if(i == len) 
			{
				if( index < UNCHOKE_COUNT ) 
				{
					now_fast[index] = p; 
					index++; 
				} 
				else 
				{
					j = get_last_index(now_fast,UNCHOKE_COUNT);
					if(p->down_rate >= now_fast[j]->down_rate) 
						now_fast[j] = p;
				}
			}
		}
		p = p->next;
	}

	// 假设now_fast中所有的peer都是要unchoke的
	for(i = 0; i < index; i++) 
	{
		Peer*  q = now_fast[i];
		unchoke_socket[i] = q->socket;
	}

	// 假设unchoke_peers.unchkpeer中所有peer都是choke的
	for(i = 0; i < unchoke_peers.count; i++) 
	{
		Peer*  q = (unchoke_peers.unchkpeer)[i];
		choke_socket[i] = q->socket;//存放刚刚被阻塞的Peer的socket
	}

	// 如果now_fast某个元素已经存在于unchoke_peers.unchkpeer
	// 则没有必要进行choke或unckoke
	for(i = 0; i < index; i++) 
	{
		if( is_in_unchoke_peers(now_fast[i]) == 1) 
		{
			for(j = 0; j < len; j++) 
			{
				Peer*  q = now_fast[i];
				if(q->socket == unchoke_socket[i])  
					unchoke_socket[i] = -1;
				if(q->socket == choke_socket[i])    
					choke_socket[i]   = -1;
			}
		}
	}

	// 更新当前unchoke的peer
	for(i = 0; i < index; i++) 
	{
		(unchoke_peers.unchkpeer)[i] = now_fast[i];
	}
	unchoke_peers.count = index;

	// 状态变化后,要对peer的状态值重新赋值,并且创建choke、unchoke消息
	p = peer_head;
	while(p != NULL) 
	{
		for(i = 0; i < len; i++) 
		{
			if(unchoke_socket[i]==p->socket && unchoke_socket[i]!=-1) 
			{
				p->am_choking = 0;//将Peer解除阻塞
				create_chock_interested_msg(1,p);//创建unchock消息
			}
			if(choke_socket[i]==p->socket && unchoke_socket[i]!=-1) 
			{
				p->am_choking = 1;//将Peer阻塞
				cancel_requested_list(p);//撤销请求队列
				create_chock_interested_msg(0,p);//创建chock消息
			}
		}
		p = p->next;
	}

	//for(i = 0; i < unchoke_peers.count; i++)
	//	printf("unchoke peer:%s \n",(unchoke_peers.unchkpeer)[i]->ip);

	return 0;
}

// 假设要下载的文件共有100个piece
// 以下函数的功能是将0到99这100个数的顺序以随机的方式打乱
// 从而得到一个随机的数组,该数组以随机的方式存储0～99,供片断选择算法使用
int *rand_num = NULL;
int get_rand_numbers(int length)
{
	int i, index, piece_count, *temp_num;
	
	if(length == 0)  
		return -1;
	piece_count = length;//获得待下载piece的数量
	
	rand_num = (int *)malloc(piece_count * sizeof(int));
	if(rand_num == NULL)    
		return -1;
	
	temp_num = (int *)malloc(piece_count * sizeof(int));
	if(temp_num == NULL)    
		return -1;
	for(i = 0; i < piece_count; i++)  
		temp_num[i] = i;
	
	srand(time(NULL));
	for(i = 0; i < piece_count; i++) 
	{
	  index = (int)( (float)(piece_count-i) * rand() / (RAND_MAX+1.0) );
		rand_num[i] = temp_num[index];
	  temp_num[index] = temp_num[piece_count-1-i];
  }
	
	if(temp_num != NULL)  
		free(temp_num);
	return 0;
}

/*
*功能：从peer队列中选择一个优化非阻塞peer
*传入参数：
*传出参数：
*返回值：
*/
int select_optunchoke_peer()
{
	int   count = 0, index, i = 0, j, ret;
	Peer  *p = peer_head; 

	// 获取peer队列中peer的总数
	while(p != NULL) 
	{
		count++;
		p =  p->next;
	}

	// 如果peer总数太少(小于等于4),则没有必要选择优化非阻塞peer
	if(count <= UNCHOKE_COUNT)  
		return 0;

	//获得一个count的随机数组
	ret = get_rand_numbers(count);
	if(ret < 0) 
	{
		printf("%s:%d get rand numbers error\n",__FILE__,__LINE__);
		return -1;
	}
	while(i < count) 
	{
		// 随机选择一个数,该数在0～count-1之间
		index = rand_num[i];

		p = peer_head;
		j = 0;
		
		//随机选择一个Peer
		while(j < index && p != NULL) 
		{
			p = p->next;
			j++;
		}

		if( is_in_unchoke_peers(p) != 1 && is_seed(p) != 1 && p->state == DATA &&
			p != unchoke_peers.optunchkpeer && is_interested(bitmap,&(p->bitmap)) ) 
		{
		
			if( (unchoke_peers.optunchkpeer) != NULL ) 
			{
				Peer  *temp = peer_head;
				//从Peer链表中找到旧的优化非阻塞Peer并将旧的阻塞
				while( temp != NULL ) 
				{
					if(temp == unchoke_peers.optunchkpeer) 
						break;
					temp = temp->next;
				}
				
				if(temp != NULL) 
				{
					(unchoke_peers.optunchkpeer)->am_choking = 1;
					create_chock_interested_msg(0,unchoke_peers.optunchkpeer);
				}
			}
			
			//更新优化非阻塞Peer
			p->am_choking = 0;
			create_chock_interested_msg(1,p);
			unchoke_peers.optunchkpeer = p;
			//printf("*** optunchoke:%s ***\n",p->ip);
			break;
		}

		i++;
	}

	//释放随机数组
	if(rand_num != NULL) 
	{ 
		free(rand_num); 
		rand_num = NULL; 
	}
	return 0;
}

/*
*功能：计算最近一段时间(如10秒)每个peer的上传下载速度
*传入参数：
*传出参数：
*返回值：
*/
int compute_rate()
{
	Peer *p = peer_head;
	time_t  time_now = time(NULL);//当前时间
	long t = 0;

	while(p != NULL) 
	{
		//计算下载速度
		//最近下载数据开始时间
		if(p->last_down_timestamp == 0) 
		{
			p->down_rate  = 0.0f;
			p->down_count = 0;
		} 
		else 
		{
			//获得下载时间
			t = time_now - p->last_down_timestamp;
			if(t == 0)  
				printf("%s:%d time is 0\n",__FILE__,__LINE__);
			else  
				p->down_rate = p->down_count / t;
				
			p->down_count = 0;
			p->last_down_timestamp = 0;
		}

		//计算上载速度
		if(p->last_up_timestamp == 0) 
		{
			p->up_rate  = 0.0f;
			p->up_count = 0;
		} 
		else 
		{
			t = time_now - p->last_up_timestamp;
			if(t == 0)  
			{
				printf("%s:%d time is 0\n",__FILE__,__LINE__);
			}
			else  
			{
				p->up_rate = p->up_count / t;
			}
			p->up_count          = 0;
			p->last_up_timestamp = 0;
		}

		p = p->next;
	}

	return 0;
}

/*
*功能：计算总的上传下载文件的大小和总的上传下载速度
*传入参数：
*传出参数：
*返回值：
*/
int compute_total_rate()
{
	Peer *p = peer_head;

	total_peers     = 0;
	total_down      = 0;
	total_up        = 0;  
	total_down_rate = 0.0f;
	total_up_rate   = 0.0f;

	while(p != NULL) 
	{
		total_down      += p->down_total;
		total_up        += p->up_total;
		total_down_rate += p->down_rate;
		total_up_rate   += p->up_rate;

		total_peers++;
		p = p->next;
	}

	return 0;
}

/*
*功能：根据位图判断某peer是否为种子，若各个位为1，则说明是种子
*传入参数：
*传出参数：
*返回值：
*/
int is_seed(Peer *node)
{
	int i;
	unsigned char  c = (unsigned char)0xFF, last_byte;
	unsigned char  cnst[8] = { 255, 254, 252, 248, 240, 224, 192, 128 };
	
	if(node->bitmap.bitfield == NULL)  
	{
		return 0;
	}
	
	//判断位图除最后一个不完全字节中是否全是oxff
	for(i = 0; i < node->bitmap.bitfield_length-1; i++) 
	{
		if( (node->bitmap.bitfield)[i] != c ) 
			return 0;//不是完整文件，不能作为种子
	}
		
	// 获取位图的最后一个字节
	last_byte = node->bitmap.bitfield[i];
	// 获取最后一个字节的无效位数
	i = 8 * node->bitmap.bitfield_length - node->bitmap.valid_length; 
	// 判断最后一个是否位种子的最后一个字节
	if(last_byte >= cnst[i]) 
		return 1;//不完整位全部为1，是种子文件
	else 
		return 0;
}

/*
*功能：生成request请求消息,实现了片断选择算法,17为一个request消息的固定长度
*传入参数：
*传出参数：
*返回值：
*/
int create_req_slice_msg(Peer *node)
{
	int index, begin, length = 16*1024;
	int i, count = 0;

	if(node == NULL)  return -1;
	// 如果被peer阻塞或对peer不感兴趣,就没有必要生成request消息
	if(node->peer_choking==1 || node->am_interested==0 )  
		return -1;

	// 如果之前向该peer发送过请求,则根据之前的请求构造新请求
	// 遵守一条原则：同一个piece的所有slice应该从同一个peer处下载
	Request_piece *p = node->Request_piece_head, *q = NULL;
	if(p != NULL) 
	{
		while(p->next != NULL)  
		{ 
			p = p->next; 
		} // 定位到最后一个结点处

		// 一个piece的最后一个slice的起始下标
		int last_begin = piece_length - 16*1024;
		// 如果是最后一个piece
		if(p->index == last_piece_index) 
		{
			last_begin = (last_piece_count - 1) * 16 * 1024;
		}
		
		// 当前piece还有未请求的slice,则构造请求消息
		if(p->begin < last_begin) 
		{
			index = p->index;
			begin = p->begin + 16*1024;
			count = 0;

			while(begin!=piece_length && count<1) 
			{
				// 如果是最后一个piece的最后一个slice
				if(p->index == last_piece_index) 
				{
					if( begin == (last_piece_count - 1) * 16 * 1024 )
						length = last_slice_len;
				}

				create_request_msg(index,begin,length,node);
				
				q = (Request_piece *)malloc(sizeof(Request_piece));
				if(q == NULL) 
				{ 
					printf("%s:%d error\n",__FILE__,__LINE__);
					return -1;
				}
				q->index = index;
				q->begin = begin;
				q->length = length;
				q->next = NULL;
				p->next = q;
				p = q;

				begin += 16*1024;
				count++;
			}
			
			return 0;  // 构造完毕,就返回
		}
	}

	// 然后去btcache_head中寻找这样的piece:它没有下载完,但它不在任何peer的
	// request消息队列中,应该优先下载这样的piece,出现这样的piece的原因是:
	// 从一个peer处下载一个piece,还没下载完,那个peer就将我们choke了或下线了

	// 但是测试结果表明, 以这种方式这种方式创建rquest请求执行效率并不高
	// 如果直接丢弃未下载完成的piece,则没有必要进行这种生成请求的方式
	// int ret = create_req_slice_msg_from_btcache(node);
	// if(ret == 0) return 0;

	// 开始对一个未请求过的piece发出请求
	// 生成随机数
	if(get_rand_numbers(pieces_length/20) == -1) 
	{
		printf("%s:%d error\n",__FILE__,__LINE__);
		return -1;
	}
	// 随机选择一个piece的下标,该下标所代表的piece应该没有向任何peer请求过
	for(i = 0; i < pieces_length/20; i++) 
	{
		//随机选择一个待下载piece
		index = rand_num[i];

		// 判断对于以index为下标的piece,peer是否拥有
		if( get_bit_value(&(node->bitmap),index) != 1)  
			continue;
		// 判断对于以index为下标的piece,是否已经下载
		if( get_bit_value(bitmap,index) == 1) 
			continue;

		// 判断对于以index为下标的piece,是否已经请求过了
		Peer          *peer_ptr = peer_head;
		Request_piece *reqt_ptr;
		int           find = 0;
		while(peer_ptr != NULL) 
		{
			reqt_ptr = peer_ptr->Request_piece_head;
			while(reqt_ptr != NULL) 
			{
				if(reqt_ptr->index == index)  
				{ 
					find = 1; 
					break; 
				}
				reqt_ptr = reqt_ptr->next;
			}
			if(find == 1) 
				break;

			peer_ptr = peer_ptr->next;
		}
		if(find == 1) 
			continue;

		break; // 程序若执行到此处,说明已经找到一个复合要求的index
	}
	if(i == pieces_length/20) 
	{
		if(end_mode == 0)  
			end_mode = 1;//下载已完成
		for(i = 0; i < pieces_length/20; i++) 
		{
			if( get_bit_value(bitmap,i) == 0 )  
			{ 
				index = i; 
				break; 
			}
		}
		
		if(i == pieces_length/20) 
		{
			printf("Can not find an index to IP:%s\n",node->ip);
			return -1;
		}
	}

	// 构造piece请求消息
	begin = 0;
	count = 0;
	p = node->Request_piece_head;
	if(p != NULL)
		while(p->next != NULL)  
			p = p->next;
	while(count < 4) 
	{
		// 如果是构造最后一个piece的请求消息
		if(index == last_piece_index) 
		{
			if(count+1 > last_piece_count) 
				break;
			if(begin == (last_piece_count - 1) * 16 * 1024)
				length = last_slice_len;
		}
		
		// 创建request消息
		create_request_msg(index,begin,length,node);

		// 将请求记录到请求队列
		q = (Request_piece *)malloc(sizeof(Request_piece));
		if(q == NULL) 
		{ 
			printf("%s:%d error\n",__FILE__,__LINE__); 
			return -1; 
		}
		q->index  = index;
		q->begin  = begin;
		q->length = length;
		q->next   = NULL;
		if(node->Request_piece_head == NULL)  
		{ 
			node->Request_piece_head = q; 
			p = q; 
		}
		else  
		{ 
			p->next = q; 
			p = q;
		}
		//printf("*** create request index:%-6d begin:%-6x to IP:%s ***\n",
		//	index,q->begin,node->ip);
		begin += 16*1024;
		count++;
	}

	//释放产生随机数的堆空间
	if(rand_num != NULL)  
	{ 
		free(rand_num); 
		rand_num = NULL; 
	}
	return 0;
}

// 以下这个函数实际并未调用,若要使用需先在头文件中声明
/*
*功能：
*传入参数：
*传出参数：
*返回值：
*/
int create_req_slice_msg_from_btcache(Peer *node)
{
	// 指针b用于遍历btcache缓冲区
	// 指针b_piece_first指向每个piece第一个slice处
	// slice_count指明一个piece含有多少个slice
	// valid_count指明一个piece中已下载的slice数
	Btcache *b = btcache_head, *b_piece_first;
	Peer *p;
	Request_piece  *r;
	int slice_count = piece_length / (16*1024);
	int count = 0, num, valid_count;
	int index = -1, length = 16*1024;
	
	while(b != NULL) {
		if(count%slice_count == 0) {
			num = slice_count;
			b_piece_first = b;
			valid_count = 0;
			index = -1;
			
			// 遍历btcache中一个piece的所有slice
			while(num>0 && b!=NULL) {
				if(b->in_use==1 && b->read_write==1 && b->is_writed==0)
					valid_count++;
				if(index==-1 && b->index!=-1) index = b->index;
				num--;
				count++;
				b = b->next;
			}
			
			// 找到一个未下载完piece
			if(valid_count>0 && valid_count<slice_count) {
				// 检查该piece是否存在于某个peer的请求队列中
				p = peer_head;
				while(p != NULL) {
					r = p->Request_piece_head;
					while(r != NULL) {
						if(r->index==index && index!=-1) break;
						r = r->next;
					}
					if(r != NULL) break;
					p = p->next;
				}
				// 如果该piece没有存在于任何peer的请求队列中,那么就找到了需要的piece
				if(p==NULL && get_bit_value(&(node->bitmap),index)==1) {
					int request_count = 5;
					num = 0;
					// 将r定位到peer最后一个请求消息处
					r = node->Request_piece_head;
					if(r != NULL) {
						while(r->next != NULL) r = r->next;
					}
					while(num<slice_count && request_count>0) {
						if(b_piece_first->in_use == 0) {
							create_request_msg(index,num*length,length,node);
							
							Request_piece *q;
							q = (Request_piece *)malloc(sizeof(Request_piece));
							if(q == NULL) { 
								printf("%s:%d error\n",__FILE__,__LINE__);
								return -1;
							}
							q->index = index;
							q->begin = num*length;
							q->length = length;
							q->next = NULL;
							printf("create request from btcache index:%-6d begin:%-6x\n",
								index,q->begin);
							if(r == NULL) {
								node->Request_piece_head = q;
								r = q;
							} else{
								r->next = q;
								r = q;
							}
							request_count--;
						}
						num++;
						b_piece_first = b_piece_first->next;
					}
					return 0;
				}
			}
		}
	}
	
	return -1;
}