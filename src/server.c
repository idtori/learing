#include"log.h"
#include"handler.h"
#include"server.h"
#include"camera.h"
#include"stream.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include<sys/socket.h>
#include<netinet/in.h>
#define PORT 8080
#define BUF_SIZE 4096

extern volatile int running;
static volatile int server_running = 1;
static int server_fd = -1;
//主线程
//创建socket
int server_init(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0)
    {
        perror("socket");
        return -1;
    }
    int opt = 1;
//                            选项归属等级  选项名字
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    //绑定地址端口
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return -1;
    }
    if(listen(server_fd ,127) < 0)
    {
        perror("listen");
        close(server_fd);
        return -1;
    }
    return server_fd;
}
void server_run(int fd)
{
    log_msg("服务器启动，监听端口：%d", PORT);
    while(1)
    {
        struct sockaddr_in addr_client;
        int *p = malloc(sizeof(int));
        if(!p)
        {
            perror("malloc");
        }
        socklen_t len = sizeof(addr_client);
        *p = accept(fd, (struct sockaddr*)&addr_client, &len);
        if(*p < 0)
        {
            perror("accept");
            free(p);
            continue;
        }
        log_msg("新连接%s %d", inet_ntoa(addr_client.sin_addr), ntohs(addr_client.sin_port));//客户端的地址和端口
        pthread_t tid;//线程
        if(pthread_create(&tid, NULL, client_handler_thread, p) != 0)
        {
            perror("pthread_creat");
            close(*p);
            free(p);
            continue;
        }
        pthread_detach(tid);
    }
}
static void *client_handler_thread(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);
    handle_client(&client_fd);
    close(client_fd);
    return NULL;
}
void server_stop(int fd)
{
    if(fd >= 0)
    {
        close(fd);
        log_msg("服务器已停止");
    }
}
