//log.h
#ifndef LOG_H
#defien LOG_H
#include <stdarg.h>

//���ڼ�¼������ļ�
void logcmd(char *fmt,...);

//����־�ļ�
int init_logfile(char *filename);

//������������־��¼���ļ�
int logfile(char *file, int line, char *msg);

#endif