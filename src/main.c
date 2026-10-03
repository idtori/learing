#include"log.h"
#include"handler.h"
#include"server.h"
#include"camera.h"
#include"stream.h"
#include <stdio.h>
#include <stdlib.h>
#include<time.h>
#include<unistd.h>
#include<signal.h>
#include<pthread.h>
#include<sys/socket.h>
#include<stdint.h>
#define PORT 8080
volatile int running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void *capture_thread(void *arg)
{
    camera_t *cam = (camera_t *)arg;
    struct timespec t1, t2;
    uint64_t frame_count = 0;
    const int fps = 30;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    while (running)
    {
        size_t buffer_size = 0;
        const void *data = camera_capture(cam, &buffer_size);
        if (data)
        {
            stream_update(data, buffer_size);
            frame_count++;
        }
        // 【修复】原来写的是 frame_count % fps == 0：循环一开始 frame_count 还是 0 就先命中一次，
        // 拿 0 帧去算帧率，打印出 "Actual FPS: 0.00"（而且 elapsed 可能为 0，会算出 inf/nan）
        if(frame_count >= (uint64_t)fps)
        {
        clock_gettime(CLOCK_MONOTONIC, &t2);
        double elapsed = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9;
        if(elapsed > 0)     // 【修复】加除零保护
        {
            double actual_fps = frame_count / elapsed;
            log_msg("Actual FPS: %.2f", actual_fps);
        }
        frame_count = 0;
        t1 = t2;
        }
    }
    
    return NULL;
}

int main()
{
    // 【修复】原来用 signal(SIGINT, on_signal)：glibc 的 signal() 等价于带 SA_RESTART 的 sigaction，
    // accept() 被 Ctrl+C 打断后会被内核自动重启，server_run 里的循环永远退不出来，
    // 下面第 95 行往后（join 采集线程、保存截图、释放资源）全是死代码。
    // 改成不带 SA_RESTART 的 sigaction，accept() 才会返回 EINTR，配合 server.c 里的 errno 判断退出
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);   // 【修复】顺带支持 kill 退出

    /* 【修复】忽略 SIGPIPE。
     * 浏览器刷新 / 关标签页 / curl 超时都会断开 TCP，此时服务器如果再往这个 socket
     * send()，内核会先给进程发 SIGPIPE —— 它的默认动作是"直接杀死整个进程"，
     * 于是现象就是：浏览器一刷新，服务器就没了（日志没有任何报错，进程凭空消失）。
     * 忽略它以后，send() 只会返回 -1/EPIPE，代码里 send_all() 已经会返回失败并结束该连接。
     * （另一种等价写法是在 send() 时加 MSG_NOSIGNAL 标志） */
    struct sigaction sa_ign = {0};
    sa_ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa_ign, NULL);

    camera_t *cam = camera_open("/dev/video0", 640, 480, 30);
    if(!cam)
    {
        log_msg("camera open failed");
        /* 【新增】面向初学者的排查提示：V4L2 摄像头是"独占设备"，
         * 同一个 /dev/video0 同一时间只能被一个进程打开，最常见的失败原因就是被旧进程占着 */
        fprintf(stderr,
                "  排查三步：\n"
                "   1) ls -l /dev/video*                             # 设备节点在不在、属主是不是 root:video\n"
                "   2) pgrep -a server ; pkill -9 -x server          # 旧的 server 没退干净会一直占着摄像头(Device or resource busy)\n"
                "   3) v4l2-ctl --list-formats-ext -d /dev/video0    # 摄像头支持哪些格式/分辨率\n");
        return 1;
    }

    if(stream_init() < 0)
    {
        camera_close(cam);
        return 1;
    }

    // 1. 先初始化服务器（bind+listen，还不进入服务循环）
    int listen_fd = server_init(PORT);
    if(listen_fd < 0)
    {
        stream_destroy();
        camera_close(cam);
        return 1;
    }

    // 2. 先启动采集线程！！重点：线程要在 server_run 之前创建
    pthread_t thread_id;
    if(pthread_create(&thread_id, NULL, capture_thread, cam) != 0)
    {
        perror("pthread_create");
        stream_destroy();
        camera_close(cam);
        return 1;
    }

    printf("Press Ctrl+C to stop the server...\n");

    // 3. 主线程进入HTTP服务死循环
    server_run(listen_fd);

    // -------------------- 走到这里 = server_run 返回了 / 收到SIGINT --------------------
    running = 0;          // 通知采集线程退出
    pthread_join(thread_id, NULL);  // 等采集线程干净退出

    // 可选：退出前保存一张图（如果你要这个功能）
    size_t buffer_size = 0;
    const void *data = camera_capture(cam, &buffer_size);
    if(data && buffer_size > 0)
    {
        FILE *fp = fopen("capture.jpg", "wb");
        if(fp)
        {
            fwrite(data, 1, buffer_size, fp);
            fclose(fp);
            printf("Captured image saved to capture.jpg\n");
        }
    }

    printf("总帧数: %lu\n", stream_frame_count());

    // 资源按顺序释放
    stream_destroy();
    camera_close(cam);

    return 0;
}
