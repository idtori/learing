#include "handler.h"
#include "stream.h"
#include "serial.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdint.h>

#define WWW_ROOT "www"
#define BUF_SIZE 4096

extern volatile int running;

// 发送所有数据的辅助函数
static int send_all(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    const char *p = (const char *)buf;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

// 处理 MJPEG 流请求
static void serve_stream(int fd) 
{
    const char *head = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: multipart/x-mixed-replace; boundary=frameboundary\r\n"
                       "Cache-Control: no-cache\r\n"
                       "Connection: close\r\n\r\n";
    if (send_all(fd, head, strlen(head)) != 0) return;

    uint8_t frame[MAX_FRAME];
    size_t len;
    uint32_t ver = 0;
    char part[256];

    while (running) 
    {
        if (!stream_get(frame, sizeof(frame), &len, &ver)) continue;

        int n = snprintf(part, sizeof(part),
                         "\r\n--frameboundary\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "Content-Length: %zu\r\n\r\n", len);
        if (send_all(fd, part, n) != 0) break;
        if (send_all(fd, frame, len) != 0) break;
    }

    const char *end = "\r\n--frameboundary--\r\n";
    send_all(fd, end, strlen(end));
}

static void serve_shot(int fd) {
    static int seq = 0;
    char path[256];
    snprintf(path, sizeof(path), "shot_%04d.jpg", ++seq);

    char resp[512];
    if (stream_save_snapshot(path) == 0)
        snprintf(resp, sizeof(resp),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n"
                 "截图已保存: %s", path);
    else
        snprintf(resp, sizeof(resp),
                 "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                 "截图失败（尚未抓到帧）");
    send_all(fd, resp, strlen(resp));
}

static void serve_ptz(int fd, const char *query) {
    const char *p = strstr(query, "dir=");
    char dir[16] = "stop";
    if (p) sscanf(p + 4, "%15[^&]", dir);

    serial_move(dir);

    char resp[256];
    snprintf(resp, sizeof(resp),
             "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n"
             "云台转向: %s", dir);
    send_all(fd, resp, strlen(resp));
}

static void serve_file(int fd, const char *path) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s%s", WWW_ROOT, path);
    if (strstr(filepath, "..")) {
        const char *e = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\n\r\nForbidden";
        send_all(fd, e, strlen(e));
        return;
    }

    int f = open(filepath, O_RDONLY);
    if (f < 0) {
        const char *e = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\n404";
        send_all(fd, e, strlen(e));
        return;
    }

    const char *ct = "application/octet-stream";
    if (strstr(filepath, ".html")) ct = "text/html; charset=utf-8";
    else if (strstr(filepath, ".css")) ct = "text/css";
    else if (strstr(filepath, ".js"))  ct = "application/javascript";
    else if (strstr(filepath, ".jpg") || strstr(filepath, ".jpeg")) ct = "image/jpeg";
    else if (strstr(filepath, ".png")) ct = "image/png";

    char head[256];
    snprintf(head, sizeof(head),
             "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nConnection: close\r\n\r\n", ct);
    send_all(fd, head, strlen(head));

    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(f, buf, sizeof(buf))) > 0) {
        if (send_all(fd, buf, n) != 0) break;
    }
    close(f);
}


void handle_client(int fd) {
    char req[BUF_SIZE];
    ssize_t n = read(fd, req, sizeof(req) - 1);
    if (n <= 0) { close(fd); return; }
    req[n] = '\0';

    char method[8] = "", path[256] = "/";
    sscanf(req, "%7s %255s", method, path);
    printf("[http] %s %s\n", method, path);

    if (strcmp(method, "GET") == 0 && strcmp(path, "/stream") == 0) {
        serve_stream(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/shot") == 0) {
        serve_shot(fd);
    } else if ((strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0)
               && strncmp(path, "/ptz", 4) == 0) {
        serve_ptz(fd, path + 4);
    } else {
        serve_file(fd, path);
    }

    close(fd);
}