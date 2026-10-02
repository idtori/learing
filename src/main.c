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
        if(frame_count % fps == 0)
        {
        clock_gettime(CLOCK_MONOTONIC, &t2);
        double elapsed = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9;
        double actual_fps = frame_count / elapsed;
        log_msg("Actual FPS: %.2f", actual_fps);
        frame_count = 0;
        t1 = t2;
        }
    }
    
    return NULL;
}

int main()
{
    signal(SIGINT, on_signal);

    camera_t *cam = camera_open("/dev/video0", 640, 480, 30);
    if(!cam)
    {
        log_msg("camera open failed");
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