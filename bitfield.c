#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "parse_metafile.h"
#include "bitfield.h"

extern int pieces_length;	//pieces�������ĳ���
extern char *file_name;		//���ڵ��ļ�,����ļ���;���ڶ��ļ�,���Ŀ¼��

Bitmap *bitmap = NULL;		//ָ��λͼ
int download_piece_num = 0;		//��ǰ�����ص�piece��

// �������һ��λͼ�ļ�,���λͼ�ļ����ѻ�ȡ�����ݱ��浽bitmap
// ���һ��,�Ϳ���ʵ�ֶϵ�����,���ϴ����ص����ݲ����ڶ�ʧ

/*
*����:�����������ļ���λͼ
*�����������
*������������
*����ֵ��
*	-1  ʧ��
*	1	��ӳɹ�
*/
int create_bitfield()
{
	bitmap = (Bitmap *)malloc(sizeof(Bitmap));
	if(bitmap == NULL) 
	{ 
		printf("allocate memory for bitmap fiailed\n"); 
		return -1;
	}

	// pieces_length����20��Ϊ�ܵ�piece����ÿ��piece��hashֵ����Ϊ20���ֽڣ�
	bitmap->valid_length = pieces_length / 20;
	bitmap->bitfield_length = pieces_length / 20 / 8;
	if( (pieces_length/20) % 8 != 0 )  
		bitmap->bitfield_length++;

	bitmap->bitfield = (unsigned char *)malloc(bitmap->bitfield_length);
	if(bitmap->bitfield == NULL)  
	{ 
		printf("allocate memory for bitmap->bitfield fiailed\n");
		//�ͷŴ洢λͼ�ṹ��Ķѿռ� 
		if(bitmap != NULL)  
			free(bitmap);
		return -1;
	}

	char bitmapfile[64];
	sprintf(bitmapfile,"%dbitmap",pieces_length);
	
	int  i;
	FILE *fp = fopen(bitmapfile,"rb");
	if(fp == NULL) 
	{  // �����ļ�ʧ��,˵����ʼ����һ��ȫ�µ�����
		memset(bitmap->bitfield, 0, bitmap->bitfield_length);
	} 
	else 
	{
		fseek(fp,0,SEEK_SET);
		for(i = 0; i < bitmap->bitfield_length; i++)
			(bitmap->bitfield)[i] = fgetc(fp);
		fclose(fp); 
		// ��download_piece_num���µĳ�ֵ
		download_piece_num = get_download_piece_num();
	}
	
	return 0;
}

/*
*����:��ȡλͼ��ĳһλ��ֵ��
*�����������
*������������
*����ֵ��
*	λͼ��ĳһλ��ֵ
*   -1  ʧ��
*/
int get_bit_value(Bitmap *bitmap, int index)
{
	int ret;
	int byte_index;
	unsigned char byte_value;
	unsigned char inner_byte_index;
	
	//����ֵԽ��
	if(bitmap == NULL || index >= bitmap->valid_length)
		return -1;
	byte_index = index / 8;
	byte_value = bitmap->bitfield(byte_index);
	inner_byte_index = index % 8;
	
	byte_value = byte_value >> (7 - inner_byte_index);
	if(byte_value % 2 == 0)
	{
		ret = 0;
	}
	else
	{
		ret = 1;
	}
	
	return ret;
}

/*
*����:����λͼ��ĳһλ��ֵ
*���������bitmap ��
*������������
*����ֵ��
*	0   �ɹ�
*   -1  ʧ��
*/
int set_bit_value(Bitmap *bitmap, int index, unsigned char v)
{
	int           byte_index;
	unsigned char inner_byte_index;

	//����Խ��
	if(index >= bitmap->valid_length)  
		return -1;
	
	//ֵ��0/1
	if((v != 0) && (v != 1))  
		return -1;

	byte_index = index / 8;
	inner_byte_index = index % 8;

	v = v << (7 - inner_byte_index);
	bitmap->bitfield[byte_index] = bitmap->bitfield[byte_index] | v;

	return 0;	
}

/*
*����:��λͼ����λ��0
*���������bitmap 
*������������
*����ֵ��
*	0   �ɹ�
*   -1  ʧ��
*/
int all_zero(Bitmap *bitmap)
{
	if(bitmap->bitfield == NULL)
	{
		return -1;
	}
	memset(bitmap->bitfield,0,bitmap->bitfield_length);
	return 0;	
}

/*
*����:��λͼ����λ��1
*���������bitmap 
*������������
*����ֵ��
*	0 �ɹ�
*	-1 ʧ��
*/
int all_set(Bitmap *bitmap)
{
	if(bitmap->bitfield == NULL)
	{
		return -1;
	}
	memset(bitmap->bitfield,0xff,bitmap->bitfield_length);
	return 0;	
}

/*
*����:�ͷű�ģ��������Ķ�̬�ڴ�
*����������� 
*������������
*����ֵ��
*/
void release_memory_in_bitfield()
{
	if(bitmap->bitfield != NULL) free(bitmap->bitfield);
	if(bitmap != NULL)  free(bitmap);
}

/*
*����:��ӡλͼ���ڵ��Գ���
*���������bitmap 
*������������
*����ֵ��
*	0 �ɹ�
*/
int print_bitfield(Bitmap *bitmap)
{
	int i;

	for(i = 0; i < bitmap->bitfield_length; i++) 
	{
		printf("%.2X ",bitmap->bitfield[i]);
		if( (i+1) % 16 == 0)  
			printf("\n");
	}
	printf("\n");

	return 0;
}

/*
*����:����λͼ���ڶ̵�����
*���������wu
*������������
*����ֵ��
*	0 �ɹ�
*	-1 ʧ��
*/
int restore_bitmap()
{
	int fd;
	char bitmapfile[64];
	
	if( (bitmap == NULL) || (file_name == NULL) )  
	{
		return -1;
	}
	sprintf(bitmapfile,"%dbitmap",pieces_length);
	fd = open(bitmapfile,O_RDWR|O_CREAT|O_TRUNC,0666);
	if(fd < 0)  
	{
		return -1;
	}
	write(fd,bitmap->bitfield,bitmap->bitfield_length);
	close(fd);
	
	return 0;	
}

/*
*����:�жϾ���srcλͼ��peer�Ծ���dstλͼ��peer�Ƿ����Ȥ
*���������src dst
*������������
*����ֵ��
*   1 ����Ȥ
*	0 ���гɹ�
*	-1 ʧ��
*/
int is_interested(Bitmap *dst,Bitmap *src)
{
	unsigned char const_char[8] = { 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};
	unsigned char c1, c2;
	int           i, j;
	
	if( dst==NULL || src==NULL )  
	{
		return -1;
	}
	if( dst->bitfield==NULL || src->bitfield==NULL )  
	{
		return -1;
	}
	if( dst->bitfield_length!=src->bitfield_length ||
		dst->valid_length!=src->valid_length )
	{
		return -1;
	}
	//���dst��ĳλΪ1��src��ӦΪ0����˵��src��dst����Ȥ
	for(i = 0; i < dst->bitfield_length-1; i++) 
	{
		for(j = 0; j < 8; j++) 
		{//�Ƚ�ĳ���ֽڵ�����λ
			c1 = (dst->bitfield)[i] & const_char[j];
			c2 = (src->bitfield)[i] & const_char[j];
			if(c1>0 && c2==0) 
				return 1;
		}
	}
	
	j  = dst->valid_length % 8;
	c1 = dst->bitfield[dst->bitfield_length-1];
	c2 = src->bitfield[src->bitfield_length-1];
	for(i = 0; i < j; i++) 
	{//�Ƚ�λͼ�����һ���ֽ�
		if( (c1&const_char[i])>0 && (c2&const_char[i])==0 )
			return 1;
	}
	
	return 0;
}
/*  
    ���Ϻ����Ĺ��ܲ��Դ������£�
	����ʱ���Խ���map1.bitfield��map2.bitfield��ֵ������ֵ

	Bitmap map1, map2;
	unsigned char bf1[2] = { 0xa0, 0xa0 };
	unsigned char bf2[2] = { 0xe0, 0xe0 };
  
	map1.bitfield        = bf1;
	map1.bitfield_length = 2;
	map1.valid_length    = 11;
	map2.bitfield        = bf2;
	map2.bitfield_length = 2;
	map2.valid_length    = 11;
	  
    int ret = is_interested(&map1,&map2);	
	printf("%d\n",ret);
 */

// ��ȡ��ǰ�����ص����ܵ�piece��
int get_download_piece_num()
{
	unsigned char const_char[8] = { 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};
	int           i, j;
	
	if(bitmap==NULL || bitmap->bitfield==NULL)  return 0;
	
	download_piece_num =0;

	for(i = 0; i < bitmap->bitfield_length-1; i++) 
	{
		for(j = 0; j < 8; j++) 
		{
			if( ((bitmap->bitfield)[i] & const_char[j]) != 0) 
				download_piece_num++;
		}
	}

	unsigned char c = (bitmap->bitfield)[i]; // c���λͼ���һ���ֽ�
	j = bitmap->valid_length % 8;            // j��λͼ���һ���ֽڵ���Чλ��
	for(i = 0; i < j; i++) 
	{
		if( (c & const_char[i]) !=0 ) 
			download_piece_num++;
	}
		
	return download_piece_num;
}

