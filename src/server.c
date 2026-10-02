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
#include <errno.h>          // 【修复】下面要用 errno/EINTR，原来缺这个头文件
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>       // 【修复】inet_ntoa 声明在此头文件，原来缺了只是隐式声明（告警且不安全）
#define PORT 8080
#define BUF_SIZE 4096

extern volatile int running;

// 【修复】记录实际绑定的端口：原来 server_run 里打印的是写死的 PORT，
// 一旦用 server_init(其它端口) 启动，日志就会误导（沙箱测试里监听 18080 却打印 8080）
static int bound_port = PORT;

// 【修复】client_handler_thread 定义在第 82 行，却在第 72 行先被使用，
// 原来编译直接报 "'client_handler_thread' undeclared"，补一条前置声明即可，函数实现没动
static void *client_handler_thread(void *arg);

// 【修复】原来这里还有两句没人用的全局变量：
// static volatile int server_running = 1;  static int server_fd = -1;
// 它们只产生 -Wunused-variable 告警（server_init 里用的是同名局部变量），故删除

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
    addr.sin_port = htons(port);    // 【修复】原来写死 htons(PORT)，传进来的 port 参数被完全忽略
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
    bound_port = port;      // 【修复】记住真正绑定的端口，供 server_run 打日志用
    return server_fd;
}
void server_run(int fd)
{
    log_msg("服务器启动，监听端口：%d", bound_port);    // 【修复】原来写死 PORT，与上面那段呼应
    while(running)      // 【修复】原来写死 while(1)，收到 Ctrl+C 也退不出来，main 里 join/存图/释放资源全是死代码
    {
        struct sockaddr_in addr_client;
        int *p = malloc(sizeof(int));
        if(!p)
        {
            perror("malloc");
            continue;   // 【修复】原来只打印不返回，紧接着 *p = accept(...) 会解引用空指针直接崩
        }
        socklen_t len = sizeof(addr_client);
        *p = accept(fd, (struct sockaddr*)&addr_client, &len);
        if(*p < 0)
        {
            if(errno == EINTR && !running)  // 【修复】被 Ctrl+C 打断且已要求退出，干净跳出循环
            {
                free(p);
                break;
            }
            perror("accept");
            free(p);
            continue;
        }
        log_msg("新连接 %s %d", inet_ntoa(addr_client.sin_addr), ntohs(addr_client.sin_port));//客户端的地址和端口（【修复】原来缺空格，日志会粘在一起）
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
    handle_client(client_fd);   // 【修复】handle_client 形参是 int，原来传 &client_fd（int*）类型不符
    // 【修复】删掉这里的 close(client_fd)：handle_client 内部已经 close 过这个 fd，
    // 重复 close 有可能把别的线程刚 accept 到的同一个 fd 号误关掉
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
