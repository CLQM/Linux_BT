#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include "data.h"
#include "sha1.h"
#include "parse_metafile.h"
#include "bitfield.h"
#include "message.h"
#include "policy.h"
#include "torrent.h"

// �Զ���Ļ�������˵����
// ���û��������Ա���Ƶ����дӲ��,�Ӷ������ڱ���Ӳ��
// ÿ�����������Ĵ�СΪ16KB,Ĭ������1024�����,�ܴ�СΪ16MB
// ��������256KBΪ��λʹ��,Ҳ�����ٽ���16�����Ϊһ����һ��piece
// �±�Ϊ0��15�Ľ����һ��piece,16��31���һ��piece,��������
// Ҳ���Դ���һ��piece�Ĵ�С��Ϊ256KB�����,��һ��piece��СΪ512KB
// Ϊ�˴����ķ���,���л������ڳ�������ʱͳһ����,�ڳ������ʱ�ͷ�

// �������й��ж��ٸ�Btcache���
#define btcache_len 1024

// ���±���������parse_metafile.c�ļ�
extern  char   *file_name;
extern  Files  *files_head;
extern  int     file_length;
extern  int     piece_length;
extern  int     pieces_length;
extern  char   *pieces;

extern  Bitmap *bitmap;
extern  int     download_piece_num;
extern  Peer   *peer_head;

// ָ��һ��16MB��С�Ļ�����
Btcache *btcache_head = NULL;//1024��16K�ѿռ����ͷָ��
// ��Ŵ������ļ������һ��piece
Btcache *last_piece = NULL;//������ص�һ��piece��Btcache����ͷָ��
int      last_piece_index = 0;//���һ��piece��������
int      last_piece_count = 0;//������ص�piece����slice������
int      last_slice_len   = 0;//������ص�piece������slice�Ĵ�С

// ����ļ�������
//ָ�������д������ļ����ļ��������洢�Ķѿռ�
int *fds    = NULL;
int fds_len = 0;//�����д������ļ��ĸ���

// ��Ÿո����ص���piece������
// ���ص�һ���µ�pieceҪ�����е�peerͨ��
int have_piece_index[64]; 

// �Ƿ�������ն�ģʽ
int end_mode = 0;

/*
*���ܣ�����Btcache��㣬�����ڴ�ռ䲢�Գ�Ա��ֵ���г�ʼ��
*�����������
*����������
*����ֵ�� 	bitcache��ַ
*			or NULL
*/
Btcache* initialize_btcache_node()
{
	Btcache *node;

	node = (Btcache *)malloc(sizeof(Btcache));
	if(node == NULL) 
	{//ʧ��
		printf("%s:%d malloc error\n",__FILE__,__LINE__);
		return NULL;
	}

	node->buff = (unsigned char *)malloc(16*1024);// ָ�򻺳�����ָ��
	if(node->buff == NULL) 
	{
		if(node != NULL)  free(node);
			printf("%s:%d malloc error\n",__FILE__,__LINE__);
		return NULL;
	}

	node->index  = -1;// �������ڵ�piece�������
	node->begin  = -1;// ������piece���е���ʼλ��
	node->length = -1;// ���ݵĳ���

	node->in_use       =  0;// �û������Ƿ���ʹ����
	// �������Ǵ�Ӳ�̶���,read_writeֵΪ0
	// �����ݽ�Ҫд��Ӳ��,read_writeֵΪ1
	// �Ƿ��͸�peer�����ݻ��ǽ��յ�������
	node->read_write   = -1;
	node->is_full      =  0;// �������Ƿ���
	node->is_writed    =  0;// �������е������Ƿ��Ѿ�д�뵽Ӳ����
	node->access_count =  0;// �Ըû������ķ��ʼ���
	node->next         =  NULL;

	return node;
}

/*
*���ܣ������ܴ�СΪ16K*1024��16MB�Ļ�����
*�����������
*������������
*����ֵ��-1		ʧ��
*/
int create_btcache()
{
	int     i;
	Btcache *node, *last;

	//����1024��16K�Ķѿռ�
	for(i = 0; i < btcache_len; i++) 
	{
		node = initialize_btcache_node();
		if( node == NULL )  
		{ 
			printf("%s:%d create_btcache error\n",__FILE__,__LINE__);
			release_memory_in_btcache();
			return -1;
		}

		if( btcache_head == NULL ) 
		{//����
			btcache_head = node; 
			last = node; 
		}
		else 
		{ 
			last->next = node; 
			last = node; 
		}
	}

	//����������ص�piece��������slice
	int count = file_length % piece_length / (16*1024);
	if(file_length % piece_length % (16*1024) != 0)  
		count++;
		
	last_piece_count = count;

	//����������ص�piece������slice�Ĵ�С(���ܲ���16k)
	last_slice_len = file_length % piece_length % (16*1024);
	if(last_slice_len == 0)  
		last_slice_len = 16*1024;

	//���һ��piece��������
	last_piece_index = pieces_length / 20 -1;

	//Ϊ������ص�һ����������piece����洢count*16K�Ķѿռ�����
	//�洢����һ��piece��slice*count
	while(count > 0) 
	{
		node = initialize_btcache_node();
		if(node == NULL) 
		{
			printf("%s:%d create_btcache error\n",__FILE__,__LINE__);
			release_memory_in_btcache();
			return -1;
		}

		if(last_piece == NULL) 
		{ 
			last_piece = node; 
			last = node; 
		}
		else  
		{ 
			last->next = node;
			last = node; 
		}

		count--;
	}

	for(i = 0; i < 64; i++) 
	{
		//��ʼ�����ڴ洢�����µ�piece����ֵ������
		have_piece_index[i] = -1;
	}

	return 0;
}

/*
*���ܣ� �ͷŻ�������̬������ڴ�
*�����������
*������������
*����ֵ��
*/
void release_memory_in_btcache()
{
	Btcache *p;

	p = btcache_head;
	while(p != NULL) 
	{
		btcache_head = p->next;
		if(p->buff != NULL) 
			free(p->buff);//�ͷ�16K��slice�ѻ�����
		free(p);//�ͷŴ洢Btcache���Ļ�����
		p = btcache_head;
	}

	//�ͷ����һ�����صĲ�����piece�Ļ���ѿռ�
	release_last_piece();
	if(fds != NULL)  
		free(fds);
}


/*
*���ܣ��ͷ����һ�����صĲ�����piece�ĸ���slice����ѿռ�
*�����������
*������������
*����ֵ��
*/
void release_last_piece()
{
	Btcache *p = last_piece;

	while(p != NULL) 
	{
		last_piece = p->next;
		if(p->buff != NULL) 
			free(p->buff);
		free(p);
		p = last_piece;
	}
}

/*
*���ܣ� �ж������ļ��д����ص��ļ�����
*�����������
*������������
*����ֵ���ļ���
*/
int get_files_count()
{
	int count = 0;
	
	//���ļ�
	if(is_multi_files() == 0)  
		return 1;

	//���ļ�
	Files *p = files_head;
	while(p != NULL) 
	{
		count++;
		p = p->next;
	}
	
	return count;
}


/*
*���ܣ� 	���������ļ��е���Ϣ���������������ݵ��ļ�
*		 	ͨ��lseek��write����������ʵ�������洢�ռ�ķ���
*�����������
*������������
*����ֵ��
*/
int create_files()
{
	int  ret, i;
	char buff[1] = { 0x0 };
	
	//��ȡ�����д������ļ��ĸ���
	fds_len = get_files_count();
	if(fds_len < 0)  
		return -1;
	
	//����fds_len��int�͵Ķѿռ����ڴ洢�������ļ����ļ�������
	fds = (int *)malloc(fds_len * sizeof(int));
	if(fds == NULL)  
		return -1;
	
	
	if( is_multi_files() == 0 ) 
	{  
		// �����ص�Ϊ���ļ�
		*fds = open(file_name,O_RDWR|O_CREAT,0777);
		if(*fds < 0)  { printf("%s:%d error",__FILE__,__LINE__); return -1; }
		
		//�����������ļ���С��ȵ��ļ�		
		ret = lseek(*fds,file_length-1,SEEK_SET);
		if(ret < 0)   { printf("%s:%d error",__FILE__,__LINE__); return -1; }
		
		//���ļ���д��һ�����ַ�
		ret = write(*fds,buff,1);
		if(ret != 1)  { printf("%s:%d error",__FILE__,__LINE__); return -1; }
				
	} 
	else 
	{  // �����ص��Ƕ���ļ�				
		// �鿴Ŀ¼�Ƿ��Ѵ���,��û���򴴽�
		ret = chdir(file_name);
		if(ret < 0) 
		{
			//��������Ŀ¼
			ret = mkdir(file_name,0777);
			if(ret < 0)  
			{ 
				printf("%s:%d error",__FILE__,__LINE__); 
				return -1; 
			}
			ret = chdir(file_name);
			if(ret < 0)  
			{ 
				printf("%s:%d error",__FILE__,__LINE__);
				return -1; 
			}
		}
					
		Files *p = files_head;
		i = 0;
		
		//�������ļ�����ʱ�������ļ�Ŀ¼�µ������ļ�
		while(p != NULL) 
		{
			fds[i] = open(p->path,O_RDWR|O_CREAT,0777);
			if(fds[i] < 0) 
			{
				printf("%s:%d error",__FILE__,__LINE__); 
				return -1;
			}
					
			ret = lseek(fds[i],p->length-1,SEEK_SET);
			if(ret < 0)    
			{
				printf("%s:%d error",__FILE__,__LINE__); 
				return -1;
			}
					
			ret = write(fds[i],buff,1);
			if(ret != 1)   
			{
				printf("%s:%d error",__FILE__,__LINE__);
				return -1;
			}
				
			p = p->next;
			i++;
		} //end while
	} //end else

	return 0;
}

/*
*���ܣ�	�ж�һ��Btcache���(��һ��slice)�е�����
		Ҫд���ĸ��ļ����ĸ�λ��,��д��
*���������Btcache
*������������
*����ֵ��
*/
int write_btcache_node_to_harddisk(Btcache *node)
{
	long long     line_position;
	Files         *p;//������ļ����ص�·�����ļ����ȵĽṹ����
	int           i;

	if((node == NULL) || (fds == NULL))  
		return -1;

	// �����Ƿ����ض��ļ�����Ҫ���ص��������ݿ���һ�������ֽ���
	// line_positionָʾҪд��Ӳ�̵�����λ��
	// piece_lengthΪÿ��piece���ȣ�����������parse_metafile.c��
	
	//�����ļ���д��λ��
	line_position = node->index * piece_length + node->begin;

	if( is_multi_files() == 0 ) 
	{  
		// ������ص��ǵ����ļ�
		//���ļ���д���ļ�ƫ�Ƶ�ָ����λ��
		lseek(*fds,line_position,SEEK_SET);
		
		//��sliceд�뵽�ļ���
		write(*fds,node->buff,node->length);
		return 0;
	}

	// ���ص��Ƕ���ļ�
	if(files_head == NULL) 
	{ 
		printf("%s:%d file_head is NULL",__FILE__,__LINE__);
		return -1;
	}
	
	p = files_head;//�洢���ļ���·�����ļ����ȵĽ��
	i = 0;
	while(p != NULL) 
	{
		if((line_position < p->length) && (line_position+node->length < p->length)) 
		{
			// ��д�����������ͬһ���ļ�
			lseek(fds[i],line_position,SEEK_SET);
			write(fds[i],node->buff,node->length);
			break;
		} 
		else if((line_position < p->length) && (line_position+node->length >= p->length)) 
		{
			// ��д������ݿ�Խ�������ļ����������ϵ��ļ�
			int offset = 0;             // buff�ڵ�ƫ��,Ҳ����д���ֽ���
			int left   = node->length;  // ʣ��Ҫд���ֽ���
			
			lseek(fds[i],line_position,SEEK_SET);
			write(fds[i],node->buff,p->length - line_position);
			offset = p->length - line_position;        // offset�����д���ֽ���
			left = left - (p->length - line_position); // ����д���ֽ���
			p = p->next;                               // ���ڻ�ȡ��һ���ļ��ĳ���
			i++;                                       // ��ȡ��һ���ļ�������
			
			while(left > 0)
				if(p->length >= left) 
				{  // ��ǰ�ļ��ĳ��ȴ��ڵ���Ҫд���ֽ��� 
					lseek(fds[i],0,SEEK_SET);
					write(fds[i],node->buff+offset,left); // д��ʣ��Ҫд���ֽ���
					left = 0;
				} 
				else
				{  // ��ǰ�ļ��ĳ���С��Ҫд���ֽ���
					lseek(fds[i],0,SEEK_SET);
					write(fds[i],node->buff+offset,p->length); // д����ǰ�ļ�
					offset = offset + p->length;
					left = left - p->length;
					i++;
					p = p->next;
				}
				
				break;
		} 
		else 
		{
			// ��д������ݲ�Ӧд�뵱ǰ�ļ�
			line_position = line_position - p->length;
			i++;
			p = p->next;
		}
	} //end while
	return 0;
}

// ��Ӳ�̶������ݣ���ŵ��������У���peer��Ҫʱ���͸�peer
// �ú����ǳ�������write_btcache_node_to_harddisk
// Ҫ����piece������index����piece�е���ʼλ��begin�ͳ����Ѵ浽nodeָ��Ľڵ���
int read_slice_from_harddisk(Btcache *node)
{
	unsigned int  line_position;
	Files         *p;
	int           i;
	
	if( (node == NULL) || (fds == NULL) )  
		return -1;
	
	if( (node->index >= pieces_length/20) || (node->begin >= piece_length) ||
	    (node->length > 16*1024) )
		return -1;

	// ��������ƫ����
	line_position = node->index * piece_length + node->begin;
	
	if( is_multi_files() == 0 ) 
	{  // ������ص��ǵ����ļ�
		lseek(*fds,line_position,SEEK_SET);
		read(*fds,node->buff,node->length);
		return 0;
	}
	
	// ������ص��Ƕ���ļ�
	if(files_head == NULL)  
		get_files_length_path();//��ȡ���ļ���·��������
		
	p = files_head;
	i = 0;
	
	while(p != NULL) 
	{
		if((line_position < p->length) && (line_position+node->length < p->length)) 
		{
			// ����������������ͬһ���ļ�
			lseek(fds[i],line_position,SEEK_SET);
			read(fds[i],node->buff,node->length);
			break;
		} 
		else if((line_position < p->length) && (line_position+node->length >= p->length))
		{
			// �����������ݿ�Խ�������ļ����������ϵ��ļ�
			int offset = 0;             // buff�ڵ�ƫ��,Ҳ���Ѷ����ֽ���
			int left   = node->length;  // ʣ��Ҫ�����ֽ���

			lseek(fds[i],line_position,SEEK_SET);
			read(fds[i],node->buff,p->length - line_position);
			offset = p->length - line_position;        // offset����Ѷ����ֽ���
			left = left - (p->length - line_position); // ��������ֽ���
			p = p->next;                               // ���ڻ�ȡ��һ���ļ��ĳ���
			i++;                                       // ��ȡ��һ���ļ�������

			while(left > 0)
			{
				if(p->length >= left) 
				{  // ��ǰ�ļ��ĳ��ȴ��ڵ���Ҫ�����ֽ��� 
					lseek(fds[i],0,SEEK_SET);
					read(fds[i],node->buff+offset,left); // ��ȡʣ��Ҫ�����ֽ���
					left = 0;
				} 
				else
				{  // ��ǰ�ļ��ĳ���С��Ҫ�����ֽ���
					lseek(fds[i],0,SEEK_SET);
					read(fds[i],node->buff+offset,p->length); // ��ȡ��ǰ�ļ�����������
					offset = offset + p->length;
					left = left - p->length;
					i++;
					p = p->next;
				}
			}

			break;
		} 
		else 
		{
			// �����������ݲ�Ӧ������ǰ�ļ�
			line_position = line_position - p->length;
			i++;
			p = p->next;
		}
	}
	return 0;
}

/*
*���ܣ� ��peer������ɾ����ĳ��piece������
*���������index
*������������
*����ֵ��-1		ʧ��
*/
int delete_request_end_mode(int index)
{
	Peer          *p = peer_head;
	Request_piece *req_p, *req_q;

	if(index < 0 || index >= pieces_length/20)  return -1;

	while(p != NULL) 
	{
		//��ȡ��Peer�������ݵĶ���ͷָ��
		req_p = p->Request_piece_head;
		while(req_p != NULL) 
		{
			if(req_p->index == index) 
			{
				if(req_p == p->Request_piece_head)
					p->Request_piece_head = req_p->next;
				else 
					req_q->next = req_p->next;
				free(req_p);

				req_p = p->Request_piece_head;
				continue;
			}
			req_q = req_p;
			req_p = req_p->next;
		}
		
		p = p->next;
	}

	return 0;
}

/*
*���ܣ� ���һ��piece�������Ƿ���ȷ,����ȷ����Ӳ��
*���������sequnce   peer
*������������
*����ֵ��	-1	ʧ��
			0	c�ɹ�
*/
int write_piece_to_harddisk(int sequnce,Peer *peer)
{
	Btcache        *node_ptr = btcache_head, *p;
	unsigned char  piece_hash1[20], piece_hash2[20];
	int            slice_count = piece_length / (16*1024);
	int            index, index_copy;

	if(peer==NULL) return -1;

	int i = 0;
	while(i < sequnce) 
	{ 
		node_ptr = node_ptr->next; i++;
	}
	p = node_ptr;  // pָ��piece�ĵ�һ��slice���ڵ�btcache��㣨ͷ�巨��

	// У��piece��HASHֵ
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	while(slice_count>0 && node_ptr!=NULL) 
	{
		SHA1Update(&ctx,node_ptr->buff,16*1024);
		slice_count--;
		node_ptr = node_ptr->next;
	}
	SHA1Final(piece_hash1,&ctx);
	
	index = p->index * 20;
	index_copy = p->index;  // ���piece��index
	
	for(i = 0; i < 20; i++)  
		piece_hash2[i] = pieces[index+i];

	int ret = memcmp(piece_hash1,piece_hash2,20);
	if(ret != 0)  
	{ 
		printf("piece hash is wrong\n"); 
		return -1; 
	}
	
	node_ptr = p;
	slice_count = piece_length / (16*1024); 
	while(slice_count > 0) 
	{
		write_btcache_node_to_harddisk(node_ptr);

		// ��peer�е����������ɾ��piece����
		Request_piece *req_p = peer->Request_piece_head;
		Request_piece *req_q = peer->Request_piece_head;
		while(req_p != NULL) 
		{
			if(req_p->begin==node_ptr->begin && req_p->index==node_ptr->index)
			{
				if(req_p == peer->Request_piece_head) 
					peer->Request_piece_head = req_p->next;
				else
					req_q->next = req_p->next;
				free(req_p);
				req_p = req_q = NULL;
				break;
			}
			req_q = req_p;
			req_p = req_p->next;
		}

		node_ptr->index  = -1;
		node_ptr->begin  = -1;
		node_ptr->length = -1;
		
		node_ptr->in_use       = 0;
		node_ptr->read_write   = -1;
		node_ptr->is_full      = 0;
		node_ptr->is_writed    = 0;
		node_ptr->access_count = 0;

		node_ptr = node_ptr->next;
		slice_count--;
	}
	
	if(end_mode == 1)  delete_request_end_mode(index_copy);

	// ����λͼ
	set_bit_value(bitmap,index_copy,1);

	// ׼������have��Ϣ
	for(i = 0; i < 64; i++) 
	{
		if(have_piece_index[i] == -1) 
		{ 
			have_piece_index[i] = index_copy; 
			break; 
		}
	}

	download_piece_num++;
	if(download_piece_num % 10 == 0)  
		restore_bitmap();

	printf("%%%%%% Total piece download:%d %%%%%%\n",download_piece_num);
	printf("writed piece index:%d  total pieces:%d\n",index_copy,pieces_length/20);
	compute_total_rate();   // �����ܵ����ء��ϴ��ٶ�
	print_process_info();   // ��ӡ���ؽ�����Ϣ

	return 0;
}

/*
*���ܣ���Ӳ���϶�ȡһ��piece��p��ָ��Ļ�������
*���������btcache  index
*������������
*����ֵ��	-1	ʧ��
			0	�ɹ�
*/
int read_piece_from_harddisk(Btcache *p, int index)
{
	Btcache  *node_ptr   = p;
	int      begin       = 0;
	int      length      = 16*1024;
	int      slice_count = piece_length / (16*1024);//ÿ��piece��slice������һ��Ϊ16��
	int      ret;

	if(p==NULL || index>=pieces_length/20)  return -1;

	while(slice_count > 0) 
	{
		node_ptr->index  = index;
		node_ptr->begin  = begin;
		node_ptr->length = length;

		ret = read_slice_from_harddisk(node_ptr);
		if(ret < 0) return -1;

		node_ptr->in_use       = 1;
		node_ptr->read_write   = 0;
		node_ptr->is_full      = 1;
		node_ptr->is_writed    = 0;
		node_ptr->access_count = 0;

		begin += 16*1024;
		slice_count--;
		node_ptr = node_ptr->next;
	}

	return 0;
}

/*
*���ܣ�  ��16MB�������������ص�pieceд��Ӳ��,���������ͷŻ�����
*���������peer
*������������
*����ֵ��
*/
int write_btcache_to_harddisk(Peer *peer)
{
	Btcache          *p = btcache_head;//���btcache����ͷָ��
	int     slice_count = piece_length / (16*1024);//ÿ��piece��slice������һ��Ϊ16��
	int     index_count = 0;
	int      full_count = 0;//��¼��������slice����Ŀ
	int     first_index;

	while(p != NULL) 
	{
		if(index_count % slice_count == 0)
		{
			full_count = 0;
			first_index = index_count;
		}

		//����������ʹ���С�����Ҫд�뵽���̡��������������������е����ݻ�δд��Ĵ�����
		if( (p->in_use  == 1) && (p->read_write == 1) && 
			(p->is_full == 1) && (p->is_writed  == 0) ) 
		{
			full_count++;
		}

		if(full_count == slice_count) 
		{
			write_piece_to_harddisk(first_index,peer);
		}

		index_count++;
		p = p->next;
	}

	return 0;
}

/*
*���ܣ���������������ʱ,�ͷ���Щ��Ӳ���϶�ȡ��piece
*���������base_count
*������������
*����ֵ��
*/
int release_read_btcache_node(int base_count)
{
	Btcache           *p = btcache_head;
	Btcache           *q = NULL;
	int            count = 0;
	int       used_count = 0;
	int      slice_count = piece_length / (16*1024);

	if(base_count < 0)  return -1;

	while(p != NULL) 
	{
		if(count % slice_count == 0)  
		{ 
			used_count = 0; 
			q = p;
		}
		if(p->in_use==1 && p->read_write==0)  
			used_count += p->access_count;
		if(used_count == base_count)  
			break;  // �ҵ�һ�����е�piece
		
		count++;
		p = p->next;
	}

	if(p != NULL) 
	{
		p = q;
		while(slice_count > 0) 
		{
			p->index  = -1;
			p->begin  = -1;
			p->length = -1;
			
			p->in_use       =  0;
			p->read_write   = -1;
			p->is_full      =  0;
			p->is_writed    =  0;
			p->access_count =  0;

			slice_count--;
			p = p->next;
		}
	}

	return 0;
}

// ������һ��slice��,����Ƿ��sliceΪһ��piece���һ��
// ������д��Ӳ��,ֻ�Ըոտ�ʼ����ʱ������,������������ʹpeer��֪
/*
*���ܣ��жϺ���
*���������index sequnce
*������������
*����ֵ��
*/
int is_a_complete_piece(int index, int *sequnce)
{
	Btcache          *p = btcache_head;
	int     slice_count = piece_length / (16*1024);
	int           count = 0;
	int             num = 0;
	int        complete = 0;

	while(p != NULL) 
	{
		if( count%slice_count==0 && p->index!=index ) 
		{
			num = slice_count;
			while(num>0 && p!=NULL) 
			{ 
				p = p->next; num--;
				count++; 
			}
			continue;
		}
		if( count%slice_count!=0 || p->read_write!=1 || p->is_full!=1) 
			break;

		*sequnce = count;
		num = slice_count;
	
		while(num>0 && p!=NULL) 
		{
			if(p->index==index && p->read_write==1 && p->is_full==1)
				 complete++;
			else break;
	
			num--;
			p = p->next;
		}

		break;
	}

	if(complete == slice_count) return 1;
	else return 0;
}

/*
*���ܣ���16MB�Ļ�����������������������
*�����������
*������������
*����ֵ��
*/
void clear_btcache()
{
	Btcache *node = btcache_head;
	while(node != NULL) 
	{
		node->index  = -1;
		node->begin  = -1;
		node->length = -1;
		
		node->in_use       =  0;
		node->read_write   = -1;
		node->is_full      =  0;
		node->is_writed    =  0;
		node->access_count =  0;
		
		node = node->next;
	}
}

/*
*���ܣ�����peer����ȡ��һ��slice�洢����������
*���������index begin length buff len peer
*������������
*����ֵ��
*/
int write_slice_to_btcache(int index,int begin,int length,
						   unsigned char *buff,int len,Peer *peer)
{
	int     count = 0, slice_count, unuse_count;
	Btcache *p = btcache_head, *q = NULL;  // qָ��ÿ��piece��һ��slice
	
	if(p == NULL)  
		return -1;
		
	if(index>=pieces_length/20 || begin>piece_length-16*1024)  
		return -1;
		
	if(buff==NULL || peer==NULL)  
		return -1;

	if(index == last_piece_index) 
	{
		write_slice_to_last_piece(index,begin,length,buff,len,peer);
		return 0;
	}

	if(end_mode == 1) 
	{
		if( get_bit_value(bitmap,index) == 1 ) 
			return 0;
	}
	
	// ����������,��鵱ǰslice���ڵ�piece�����������Ƿ��Ѵ���
	// ������˵������һ���µ�piece,��������˵����һ���µ�piece
	slice_count = piece_length / (16*1024);
	while(p != NULL) 
	{
		if(count%slice_count == 0) 
			q = p;
		if(p->index==index && p->in_use==1)  
			break;

		count++;
		p = p->next;
	}
	
	// p�ǿ�˵����ǰslice���ڵ�piece����Щ�����Ѿ�����
	if(p != NULL) 
	{
		count = begin / (16*1024);  // count��ŵ�ǰҪ���slice��piece�е�����
		p = q;
		while(count > 0) 
		{ 
			p = p->next; 
			count--; 
		}
		
		if(p->begin==begin && p->in_use==1 && p->read_write==1 && p->is_full==1)
			return 0; // ��slice�Ѵ���
		
		p->index  = index;
		p->begin  = begin;
		p->length = length;
		
		p->in_use       = 1;
		p->read_write   = 1;
		p->is_full      = 1;
		p->is_writed    = 0;
		p->access_count = 0;
		
		memcpy(p->buff,buff,len);
		printf("+++++ write a slice to btcache index:%-6d begin:%-6x +++++\n",
			   index,begin);
		
		// ����Ǹոտ�ʼ����(���ص���piece����10��),������д��Ӳ��,����֪peer
		if(download_piece_num < 1000) 
		{
			int sequece;
			int ret;
			ret = is_a_complete_piece(index,&sequece);
			if(ret == 1) 
			{
				printf("###### begin write a piece to harddisk ######\n");
				write_piece_to_harddisk(sequece,peer);
				printf("###### end   write a piece to harddisk ######\n");
			}
		}
		return 0;
	}
	
	// pΪ��˵����ǰslice�������ڵ�piece�ĵ�һ�����ص�������
	// �����ж��Ƿ���ڿյĻ�����,��������,�������ص�д��Ӳ��
	int i = 4;
	while(i > 0) 
	{
		slice_count = piece_length / (16*1024);
		count       = 0;  // ������ǰָ��ڼ���slice
		unuse_count = 0;  // ������ǰpiece���ж��ٸ��յ�slice
		Btcache *q;       
		p = btcache_head;
		
		while(p != NULL) 
		{
			if(count%slice_count == 0)
			{ 
				unuse_count = 0; 
				q = p; 
			}
			
			if(p->in_use == 0) 
				unuse_count++;
				
			if(unuse_count == slice_count)  
				break;  // �ҵ�һ�����е�piece
			
			count++;
			p = p->next;
		}
		
		if(p != NULL) 
		{
			p = q;
			count = begin / (16*1024);
			while(count > 0) 
			{ 
				p = p->next; 
				count--; 
			}

			p->index  = index;
			p->begin  = begin;
			p->length = length;
			
			p->in_use       = 1;
			p->read_write   = 1;
			p->is_full      = 1;
			p->is_writed    = 0;
			p->access_count = 0;
			
			memcpy(p->buff,buff,len);
			printf("+++++ write a slice to btcache index:%-6d begin:%-6x +++++\n",
				   index,begin);
			return 0;
		}
		
		if(i == 4) write_btcache_to_harddisk(peer);
		if(i == 3) release_read_btcache_node(16);
		if(i == 2) release_read_btcache_node(8);
		if(i == 1) release_read_btcache_node(0);
		i--;
	}
	
	// �����û�п��еĻ�����,�������ص����slice
	printf("+++++ write a slice to btcache FAILED :NO BUFFER +++++\n");
	clear_btcache();

	return 0;
}


/*
*���ܣ�		�ӻ�������ȡһ��slice,��ȡ��slice��ŵ�buffָ���������
		���������в����ڸ�slice,���Ӳ�̶�slice���ڵ�piece����������
*���������index 	begin peer 
*������������
*����ֵ��
*/
int read_slice_for_send(int index,int begin,int length,Peer *peer)
{
	Btcache  *p = btcache_head, *q;  // qָ��ÿ��piece��һ��slice
	int       ret;
	
	// �������Ƿ�����
	if(index>=pieces_length/20 || begin>piece_length-16*1024)  return -1;

	ret = get_bit_value(bitmap,index);
	if(ret < 0)  
	{ 
		printf("peer requested slice did not download\n"); 
		return -1; 
	}

	if(index == last_piece_index) 
	{
		read_slice_for_send_last_piece(index,begin,length,peer);
		return 0;
	}

	// ����ȡ��slice���������Ѵ���
	while(p != NULL) 
	{
		if(p->index==index && p->begin==begin && p->length==length &&
		   p->in_use==1 && p->is_full==1) 
		{
			// ����piece��Ϣ
			ret = create_piece_msg(index,begin,p->buff,p->length,peer);
			if(ret < 0) 
			{ 
				printf("Function create piece msg error\n"); 
				return -1; 
			}
			p->access_count = 1;
			return 0;
		}
		p = p->next;
	}

	int i = 4, count, slice_count, unuse_count;
	while(i > 0) 
	{
		slice_count = piece_length / (16*1024);
		count = 0;  // ������ǰָ��ڼ���slice
		p = btcache_head;

		while(p != NULL) 
		{
			if(count%slice_count == 0)  
			{ 
				unuse_count = 0; 
				q = p; 
			}
			if(p->in_use == 0) 
				unuse_count++;
			if(unuse_count == slice_count)  
				break;  // �ҵ�һ�����е�piece
			
			count++;
			p = p->next;
		}
		
		if(p != NULL) 
		{
			read_piece_from_harddisk(q,index);

			p = q;
			while(p != NULL) 
			{
				if(p->index==index && p->begin==begin && p->length==length &&
					p->in_use==1 && p->is_full==1) 
				{
					// ����piece��Ϣ
					ret = create_piece_msg(index,begin,p->buff,p->length,peer);
					if(ret < 0) 
					{ 
						printf("Function create piece msg error\n"); 
						return -1; 
					}
					p->access_count = 1;
					return 0;
				}
				p = p->next;
			}
		}
		
		if(i == 4) write_btcache_to_harddisk(peer);
		if(i == 3) release_read_btcache_node(16);
		if(i == 2) release_read_btcache_node(8);
		if(i == 1) release_read_btcache_node(0);
		i--;
	}

	// ���ʵ��û�л�������,�Ͳ���slice���ڵ�piece����������
	p = initialize_btcache_node();
	if(p == NULL)  
	{ 
		printf("%s:%d allocate memory error",__FILE__,__LINE__); 
		return -1; 
	}
	
	p->index  = index;
	p->begin  = begin;
	p->length = length;
	read_slice_from_harddisk(p);
	// ����piece��Ϣ
	ret = create_piece_msg(index,begin,p->buff,p->length,peer);
	if(ret < 0) 
	{ 
		printf("Function create piece msg error\n"); 
		return -1; 
	}
	// �ͷŸո�������ڴ�
	if(p->buff != NULL)  free(p->buff);
	if(p != NULL) free(p);

	return 0;
}

void clear_btcache_before_peer_close(Peer *peer)
{
	Request_piece  *req = peer->Request_piece_head;
	int			   i = 0, index[2] = {-1, -1};

	if(req == NULL)  return;
	while(req != NULL && i < 2) 
	{
		if(req->index != index[i]) 
		{ 
			index[i] = req->index; 
			i++; 
		}
		req = req->next;
	}

	Btcache *p = btcache_head;
	while( p != NULL ) 
	{
		if( p->index != -1 && (p->index==index[0] || p->index==index[1]) ) 
		{
			p->index  = -1;
			p->begin  = -1;
			p->length = -1;
			
			p->in_use       =  0;
			p->read_write   = -1;
			p->is_full      =  0;
			p->is_writed    =  0;
			p->access_count =  0;
		}
		p = p->next;
	}
}


// ����������һ��piece������,�޸����¼�����
// ��data.cͷ�������˼���ȫ�ֱ���
// ��data.c���޸��˳�ʼ���䶯̬�ڴ溯���������ͷŶ�̬�ڴ�ĺ���
// ��rate.c���޸���create_req_slice_msg����
// ��data.c������������4������
int write_last_piece_to_btcache(Peer *peer)
{
	int            index = last_piece_index, i;
	unsigned char  piece_hash1[20], piece_hash2[20];
	Btcache        *p = last_piece;

	// У��piece��HASHֵ
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	while(p != NULL) 
	{
		SHA1Update(&ctx,p->buff,p->length);
		p = p->next;
	}
	SHA1Final(piece_hash1,&ctx);
	
	for(i = 0; i < 20; i++)  piece_hash2[i] = pieces[index*20+i];

	if(memcmp(piece_hash1,piece_hash2,20) == 0) 
	{
		printf("@@@@@@  last piece downlaod OK @@@@@@\n");
	}
	 else 
	{
		printf("@@@@@@  last piece downlaod NOT OK @@@@@@\n");
		return -1;
	}

	p = last_piece;
	while( p != NULL) 
	{
		write_btcache_node_to_harddisk(p);
		p = p->next;
	}
	printf("@@@@@@  last piece write to harddisk OK @@@@@@\n");

	// ��peer�е����������ɾ��piece����

	// ����λͼ
	set_bit_value(bitmap,index,1);
	
	// ׼������have��Ϣ
	for(i = 0; i < 64; i++) 
	{
		if(have_piece_index[i] == -1) 
		{ 
			have_piece_index[i] = index; 
			break; 
		}
	}

	download_piece_num++;
	if(download_piece_num % 10 == 0)  restore_bitmap();

	return 0;
}

int write_slice_to_last_piece(int index,int begin,int length,
							  unsigned char *buff,int len,Peer *peer)
{
	if(index != last_piece_index || begin > (last_piece_count-1)*16*1024)
		return -1;
	if(buff==NULL || peer==NULL)  
		return -1;

	// ��λ��Ҫд���ĸ�slice
	int count = begin / (16*1024);
	Btcache *p = last_piece;
	
	while(p != NULL && count > 0) 
	{
		count--;
		p = p->next;
	}

	if(p->begin==begin && p->in_use==1 && p->is_full==1)
		return 0; // ��slice�Ѵ���
	
	p->index  = index;
	p->begin  = begin;
	p->length = length;

	p->in_use       = 1;
	p->read_write   = 1;
	p->is_full      = 1;
	p->is_writed    = 0;
	p->access_count = 0;
	
	memcpy(p->buff,buff,len);

	p = last_piece;
	while(p != NULL) 
	{
		if(p->is_full != 1)  break;
		p = p->next;
	}
	
	if(p == NULL) 
	{
		write_last_piece_to_btcache(peer);
	}

	return 0;
}
/*
*���ܣ���Ӳ���ж����һ��piece
*���������
*����������
*����ֵ��
*/
int read_last_piece_from_harddisk(Btcache *p, int index)
{
	Btcache  *node_ptr   = p;
	int      begin       = 0;
	int      length      = 16*1024;
	int      slice_count = last_piece_count; 
	int      ret;
	
	if(p==NULL || index != last_piece_index)  return -1;
	
	while(slice_count > 0) 
	{
		node_ptr->index  = index;
		node_ptr->begin  = begin;
		node_ptr->length = length;
		if(begin == (last_piece_count-1)*16*1024) 
			node_ptr->length = last_slice_len;
		
		ret = read_slice_from_harddisk(node_ptr);
		if(ret < 0) 
			return -1;
		
		node_ptr->in_use       = 1;
		node_ptr->read_write   = 0;
		node_ptr->is_full      = 1;
		node_ptr->is_writed    = 0;
		node_ptr->access_count = 0;
		
		begin += 16*1024;
		slice_count--;
		node_ptr = node_ptr->next;
	}
	
	return 0;
}

/*
*���ܣ���buff�л�ȡ���һ��piece�����peer�ķ��ͻ�����
*���������
*����������
*����ֵ��
*/
int read_slice_for_send_last_piece(int index,int begin,int length,Peer *peer)
{
	Btcache  *p;
	int       ret, count = begin / (16*1024);
	
	// �������Ƿ�����
	if(index != last_piece_index || begin > (last_piece_count-1)*16*1024)
		return -1;
	
	ret = get_bit_value(bitmap,index);
	if(ret < 0)  
	{
		printf("peer requested slice did not download\n"); 
		return -1;
	}

	p = last_piece;
	while(count > 0) 
	{
		p = p->next;
		count --;
	}
	if(p->is_full != 1) 
	{
		ret = read_last_piece_from_harddisk(last_piece,index);
		if(ret < 0)  
			return -1;
	}
	
	if(p->in_use == 1 && p->is_full == 1) 
	{
		ret = create_piece_msg(index,begin,p->buff,p->length,peer);
	}

	if(ret == 0)  
		return 0;
	else 
		return -1;
}