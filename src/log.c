#include"log.h"
#include<stdio.h>
#include<stdarg.h>
#include<pthread.h>

//静态   互斥锁数据类型   互斥锁名字    初始化
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_msg(const char *fmt,...)
{
    //加锁
    pthread_mutex_lock(&log_mutex);
    //                                获取当前进程id
    printf("tid = %lu ",(unsigned long)pthread_self());     // 【修复】原来结尾没有分隔符，日志会粘成 "tid = 12345服务器启动"
    //定义一个参数指针
    va_list ap;
    //声明指针指向的位置
    va_start(ap, fmt);
    //输出
    vprintf(fmt, ap);
    //重置指针
    va_end(ap);

    printf("\n");
    fflush(stdout);

    //解锁
    pthread_mutex_unlock(&log_mutex);
}
