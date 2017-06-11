//parse_metafile.c  解析文件
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "parse_metafile.h"
#include "shal.h"

char *metafile_content = NULL;	//保存种子的内容
long filesize;					//种子的长度

int piece_length = 0;		//每个piece的长度一般为256KB
char *pieces = NULL;		//保存每个pieces的hash值，每个为20B
int pieces_length = 0;		//缓冲区pieces的长度

int multi_file = 0;		//指明是单文件还是多文件
char *file_name = NULL;	//对于单文件，存放文件；对于多文件，存放目录名
long long file_length = 0;	//存放待下载的文件的总长度
Files *files_head = NULL; 	//对多文件种子有效，存放各个文件的路径和长度
unsigned char info_hash[20];	//保存info_hash的值，连接tracker和peer时使用
unsigned char peer_id[20];	//保存peer_id的值，连接peer时使用

Announce_list *announce_list_head = NULL;	//用于保存所有trackr的服务器的URL

/*
* int read_metafile(char *metafile_name)
*解析种子文件
*metafile_name是种子文件名
*处理成功返回0，否则返回-1
*将种子文件的内容读入到全局变量metafile_content所指的缓存区中

*说明 编译器预定义的宏__FILE__和__LINE__在程序中可以直接使用。
*__FILE__代表该宏所在源文件的文件名，在源文件parse_metafile.c中
*该宏的值等于"parse_metafile.c",宏__LINE__的值为__LINE__所在的行
*号
*
*
*/
int read_metafile(char *metafile_name)
{
	long i;

	//以二进制、只读的方式打开文件
	FILE *fp = fopen(metafile_name, "rb");
	if(fp == NULL)
	{
		printf("%s:%d can not open file\n", __FILE__, __LINE__);
		return -1;
	}

	//获得种子文件的长度，fieszie为全局变量,在parse_metafile_name
	fseek(fp , 0, SEEK_END);
	filesize = ftell(fp);
	if(filesize == -1)
	{
		printf("%s:%d fseek failed\n", __FILE__,__LINE__);
		return -1;
	}
	metafile_content = (char *)malloc(filesize+1);
	if(metafile_content == NULL)
	{
		printf("%s:%d malloc failed\n", __FILE__,__LINE__);
		return -1;
	}

	//读取种子文件的内容到metafile_content缓冲区中
	fseek(fp, 0, SEEK_SET);
	for(i = 0; i < filesize; i++)
	{
		metafile_content[i] = fgetc(fp);
	}

	metafile_content[i] = '\0';
	fclose(fp);


#ifdef DEBUG
	printf("metafile size is: &ld\n", filesize);
#endif
	return 0;
}

/*
int find_keyword(char *keyword, long *position)
功能:从种子文件中查找某个关键字
参数:keyword为要查找的关键字，position用于返回关键字第一个字符所在的下标.
返回:成功找到关键字返回1，未找到返回0，执行失败返回-1
*/
int find_keyword(char *keyword, long *position)
{
	long i;

	*position = -1;
	if(keyword == NULL)
		return 0;

	for(i = 0; i < filesize - strlen(keyword); i++)
	{
		if( memcmp(&metafile_content[i], keyword, strlen(keyword))==0)
		{
			*position = i;
			return 1;
		}
	}
	return 0;
}

/*
功能:获得Tracker地址，并将获得的地址保存到全局变量announce_list_head指向的List中
*/
int read_announce_list()
{
	Announce_list *node = NULL;
	Announce_list *p = NULL;
	int len = 0;
	long i;

	if( find_keyword("13:announce-list", &i) == 0)
	{//无"13:announce-list"关键字
		if( find_keyword("8:announce", &i) == 1)
		{
			//跳过"8:announce"
			i = i + strlen("8:announce");	//i未加前是字符串的起始位置

			//检测字符串后边的字符是否是阿拉伯数字
			//获取URL的长度
			while(isdigit(metafile_content[i]))
			{
				len = len * 10 + (metafile_content[i] - '0');
				i++;
			}
			i++; //跳过':'

			//申请堆空间保存Tracker的URL
			node = (Announce_list *)malloc(sizeof(Announce_list));
			strcpy(node->announce, &metafile_content[i], len);
			node->announce[len] = '\0';
			node->next = NULL;
			announce_list_head = node;
		}
	}
	else
	{/*如果有13:announce-list关键字不用处理8:announce关键词
	 **使用备用的URL(备用URL中包含关键字"8:announce"包含的URL)
	 **关键字"13:announce-list"之后的第一个字符为列表的起始字符'l'
	 **该列表中含有两个元素，这两个元素的类型也都是列表
	 **
	 */
		i = i + strlen("13:announce-list");
		i++;	//跳过'1'
		while(metafile_content[i] != 'e')
		{
			i++;   //跳过'l'
			/*
			*检查输入的参数是否为阿拉伯数字
			*获取URL的长度
			*/

			while(isdigit(metafile_content[i]))
			{
				len = len * 10 + (metafile_content[i] - '0');
				i++;
			}
			if(metafile_content[i] == ':')
				i++;	//跳过':'
			else
				return -1;
			//只处理以http开头的tracker地址，不处理以udp开头的地址
			if(memcmp(&metafile_content[i], "http", 4) == 0)
			{
				node = (Announce_list *)malloc(sizeof(Announce_list));
				strcpy(node->announce, &metafile_content[i],len);
				node->announce[len] = '\0';
				node->next = NULL;

				if(announce_list_head == NULL)
				{
					announce_list_head = node;
				}
				else
				{
					p = announce_list_head;
					while(p->next != NULL)
						p = p->next;	//使p指针指向最后个节点
					p->next = node; 	//node成为tracker列表的最后一个节点
				}
			}

			i = i + len;
			len = 0;
			i++;	//跳过'e'
			if(i >= filesize)
				return -1;
		}	//while循环结束
	}
#ifdef DEBUG
	p = announce_list_head;
	while(p != NULL)
	{
		printf("%s\n", p->announce);
		p = p->next;
	}
#endif
	return 0;
}

// d8:announce32:http://tk.greedland.net/announce13:announce-list1132:http://tk.greedland.net/announcee113:http://th2.greedland.net/announceee...

/*
*功能:连接某种Tracker时会返回一个重定向的URL，需要连接URL才能获得Peer
*传入参数：新的URL
*传出参数：无
*返回值：
*	0	待添加的URL已经存在
*	1	添加成功
*/
int add_an_announce(char *url)
{
	Announce_list *p = announce_list_head, *q;

	//若参数指定的URL在Tracker列表中已存在，则无需添加
	while(p != NULL)
	{
		if(strcmp(p->announce,url) == 0)
			break;
		p=p->next;
	}
	if(p != NULL)
		return 0;
	q = (Announce_list *)malloc(sizeof(Announce_list));
	strcpy(q->announce, url);
	q->next = NULL;

	p = announce_list_head;
	if(p == NULL)
	{
		announce_list_head = q;
		return 1;
	}
	while(p->next != NULL)
	{
		p = p->next;
	}
	p->next = q;
	return 1;
}

/*
*判断下载多个文件还是单文件，若含有关键字“5:files”则说明下载的是多文件
*传入参数：无
*传出参数：无
*返回值：
*	1	表示是多文件
*/
int is_multi_files()
{
	long i;
	if( find_keyword("5:files", &i) == 1)
	{
		multi_file = 1;
		return 1;
	}
#ifdef DEBUG
	printf("is_multi_files:&d\n", multi_file);
#endif
	return 0;
}

/*
*功能：获取piece的长度
*传入参数：无
*传出参数：无
返回值：
* 0 成功
*	-1 失败
*/

int get_piece_length()
{
	long i;

	if( find_keyword("12:piece length", &i) == 1)
	{
		i = i + strlen("12:piece length");  //跳过"12:piece length"
		i++; //跳过 'i'
		while(metafile_content[i] != 'e')
		{
			piece_length = piece_length + 10 + (metafile_content[i] - '0');
			i++;
		}
	}
	else
	{
		return -1;
	}

#ifdef DEBUG
	printf("piece length:%d\n", piece_length);
#endif
	return 0;
}

/*
*功能：获取每个piece的hash值，并保存到pieces所指向的缓冲区中
*传入参数：无
*传出参数：无
返回值：
* 0 成功
*	-1 失败
*/
int get_pieces()
{
	long i;

	if( find_keyword("6:pieces", &i) == 1)
	{
		i = i + 8; 	//跳过"6:pieces"
		while(metafile_content[i] != ':')
		{
			pieces_length = piece_length * 10 + (metafile_content[i] - '0');
			i++;
		}
		i++;		//跳过':'
		pieces = (char *)malloc(pieces_length + 1);
	}
	else
	{
		return -1;
	}
#ifdef DEBUG
	printf("get_pieces ok\n");
#endif
	return 0;
}

/*
*功能：获取待下载的文件名，如果待下载的是目录则获取的是目录名
*传入参数：无
*传出参数：无
返回值：
* 0 成功
*	-1 失败
*/
int get_file_name()
{
    long i;
    int count = 0;

    if( find_keyword("4:name", &i) == 1)
    {
        i = i + 6;  //跳过"4:name"
        while(metafile_content[i] != ':')
        {
            count = count * 10 + (metafile_content[i] - '0');
            i++;
        }
        i++;    //跳过':'
        file_name = (char *)malloc(count+1);
        memcpy(file_name, &metafile_content[i], count);
        file_name[count] = '\0';
    }
    else
    {
        return -1;
    }
#ifdef DEBUG
    // 由于可能含有中文字符,因此可能打印出乱码
	// printf("file_name:%s\n",file_name);
#endif // DEBUG
    return 0;
}
/*d8:announce32:http://tk.greedland.net/announce13:announce-list1132:http://tk.greedland.net/announcee113:http;//tk2.greedland.net/announceee13:creation
datei1187968874e4:infod6:lengthi119861
306e4:name31:[ymer][naruto][246][jp_cn].rmvb10:name.utf-831:[ymer][naruto][246][jp_cn].rmv
	b12:piece lengthi262144e6:pieces9160:...ee
*/

/*
*功能：获取待下载的长度
*传入参数：无
*传出参数：无
返回值：
* 0 成功
*
*/
int get_file_length()
{
    long i;

    if(is_multi_files() == 1)
    {
        if(files_head == NULL)
        {
            get_files_length_path();
        }
        Files *p = files_head;
        while(p != NULL)
        {
            file_length += p->length;
            p = p->next;
        }
    }
    else
    {
        if( find_keyword("6:length", &i) == 1)
        {
            i = i + 8;  //跳过"6:length"
            i++;    //跳过'i'
            while(metafile_content[i] != 'e')
            {
                file_length = file_length * 10 + (metafile_content[i] - '0');
                i++;
            }
        }
    }

#ifdef DEBUG
    //printf("file_length:%1ld\n", file_length);
#endif // DEBUG
    return 0;
}

/*
*功能：对于多文件，获取各个文件的路径以及长度
*传入参数：无
*传出参数：无
*
*/
int get_files_length_path()
{
    long i;
    int length;
    int count;
    Files *node = NULL;
    Files *p = NULL;

    if(is_multi_files() != 1)
    {//是否为多文件
        return 0;
    }
    for(i = 0; i < filesize -8; i++)
    {
        //获取文件length并存储在链表中
        if(memcmp(&metafile_content[i], "6:length", 8) == 0)
        {
            i = i + 8; //跳过 "6:length"
            i++;        //跳过'i'
            length = 0;
            while(metafile_content[i] != 'e')
            {
                length = length * 10 + (metafile_content[i] - '0');
                i++;
            }
            node = (Files *)malloc(sizeof(Files));
            node->length = length;
            node->next = NULL;
            if(files_head == NULL)
            {
                files_head = node;
            }
            else
            {
                p = files_head;
                while(p->next != NULL)
                {
                    p = p->next;
                }
                p->next = node;
            }
        }
        //获取文件payh并保存存储链表中
        if(memcpy(&metafile_content[i], "4:path", 6) == 0)
        {
            i = i + 6;   // 跳过"4:path"
            i++;    //跳过'l'
            count = 0;
            while(metafile_content[i] != ':')
            {
                count = count * 10 + (metafile_content[i] - '0');
                i++;
            }
            i++;    //跳过':'
            p = files_head;
            while(p->next != NULL)
            {
                p = p->next;
                memcpy(p->path, &metafile_content[i], count);
                *(p->path + count) = '\0';
            }
        }

    }
#ifdef DEBUG
	// 由于可能含有中文字符,因此可能打印出乱码
	// p = files_head;
	// while(p != NULL) {
	//	 printf("%ld:%s\n",p->length,p->path);
	//	 p = p->next;
	// }
#endif
    return 0;
}

/*
*功能：计算info_hash的值
*传入参数：无
*传出参数：无
*返回值
    0 成功
    -1 失败
*/
int get_info_hash()
{
    int push_pop = 0;
    long i, begin, end;

    if(metafile_content == NULL)
    {
        return -1;
    }
    //begin的值表示的是关键字"4:info"对应值的其实下标
    if( find_keyword("4:info", &i) == 1)
    {
        begin = i + 6;
    }
    else
    {
        return -1;
    }
    i = i + 6;  //跳过"4:info"
    for(; i < filesize;)
    {
        if(metafile_content[i] == 'd')
        {
            push_pop++;
            i++;
        }
        else if(metafile_content[i] == 'l')
        {
            push_pop++;
            i++;
        }
        else if(metafile_content[i] == 'i')
        {
            i++;    //跳过'i'
            if(i == filesize)
            {
                return -1;
            }
            while(metafile_content[i] != 'e')
            {
                if((i + 1) == filesize)
                {
                    return -1;
                }
                else
                {
                    i++;
                }
            }
            i++;
        }
        else if((metafile_content[i] >= '0') && (metafile_content[i] <= '9'))
        {
            int number = 0;
            while((metafile_content[i] >= '0') && (metafile_content[i] <= '9'))
            {
                number = number * 10 + metafile_content[i] - '0';
                i++;
            }
            i++;        //跳过':'
            i = i + number;
        }
        else if(metafile_content[i] == 'e')
        {
            push_pop--;
            if(push_pop == 0)
            {
                end = i;
                break;
            }
            else
            {
                i++;
            }
        }
        else
        {
            return -1;
        }
    }
    if(i == filesize)
    {
        return -1;
    }
    SHA1_CTX context;
    SHA1Init(&context);
    SHA1Update(&context, &metafile_content[begin], end-begin+1);
    SHA1Final(info_hash, &context);

#ifdef DEBUG
    printf("info_hash");
    for(i = 0; i < 20; i++)
    {
        printf("&.2x ", info_hash[i]);
    }
    printf("\n");
#endif // DEBUG
    return 0;
}

/*
*功能：生成一个惟一的peer id
*传入参数：无
*传出参数：无
*返回值：0
*/

int get_peer_id()
{
    //设置产生随机数的种子
    srand(time(NULL));
    //使用rand函数生成一个随机数，并使用该随机数构造peer_id
    //peer_id前8位固定为-TT1000-
    // 生成随机数,并把其中12位赋给peer_id,peer_id前8位固定为-TT1000-
    sprintf(peer_id, "-TT1000-%12d", rand());

#ifdef DEBUG
    int i;
    printf("peer_id: ");
    for(i = 0; i < 20; i++)
    {
        printf("%c", peer_id[i]);
    }
    printf("\n");
#endif
    return 0;
}

/*
*功能：释放动态申请的内存
*传入参数：无
*传出参数：无
*返回值：无
*/
void release_memory_in_parse_metafile()
{
	Announce_list *p;
	Files         *q;

	if(metafile_content != NULL)  free(metafile_content);
	if(file_name != NULL)         free(file_name);
	if(pieces != NULL)            free(pieces);

	while(announce_list_head != NULL) {
		p = announce_list_head;
		announce_list_head = announce_list_head->next;
		free(p);
	}

	while(files_head != NULL) {
		q = files_head;
		files_head = files_head->next;
		free(q);
	}
}


/*
*功能：解析种子文件
*传入参数：metafile种子文件的路径及文件名
*传出参数：无
*返回值：0
*/
int parse_metafile(char *metafile)
{
	int ret;

	// 读取种子文件
	ret = read_metafile(metafile);
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	// 从种子文件中获取tracker服务器的地址
	ret = read_announce_list();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	// 判断是否为多文件
	ret = is_multi_files();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	// 获取每个piece的长度,一般为256KB
	ret = get_piece_length();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	// 读取各个piece的哈希值
	ret = get_pieces();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	// 获取要下载的文件名，对于多文件的种子，获取的是目录名
	ret = get_file_name();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	// 对于多文件的种子，获取各个待下载的文件路径和文件长度
	ret = get_files_length_path();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	// 获取待下载的文件的总长度
	ret = get_file_length();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	// 获得info_hash，生成peer_id
	ret = get_info_hash();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }
	ret = get_peer_id();
	if(ret < 0) { printf("%s:%d wrong",__FILE__,__LINE__); return -1; }

	return 0;
}
