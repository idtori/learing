#ifndef __SERVER_H
#define __SERVER_H

/* 【新增】整个文件都是新增的：
   原来 main.c 和 server.c 都 #include"server.h"，但仓库里从来没有这个头文件
   （git log --all -- src/server.h 也没有记录），所以项目第一步编译就 fatal error。
   这里只补上 server.c 已经实现好的 3 个接口声明，不改动任何实现。 */

//创建并绑定监听套接字，成功返回监听 fd，失败返回 -1
int server_init(int port);

//进入 accept 循环；收到 SIGINT（running = 0）后返回
void server_run(int fd);

//关闭监听套接字
void server_stop(int fd);

#endif
