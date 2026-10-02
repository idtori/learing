#include"stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include<pthread.h>

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint32_t version;
    uint8_t data[MAX_FRAME];
    size_t size;
    int ready;
    int stop;               // 【修复】新增退出标志：主线程要退出时用它唤醒所有等帧的线程
    uint32_t frame_count;
} frame_t;

static frame_t frame;

int stream_init()
{
    memset(&frame, 0, sizeof(frame));
    pthread_mutex_init(&frame.mutex, NULL);
    pthread_cond_init(&frame.cond, NULL);
    frame.version = 1;
    frame.ready = 0;
    frame.stop = 0;         // 【修复】初始化退出标志（memset 已置 0，这里写出来更清楚）
    frame.frame_count = 0;
    return 0;
}

void stream_update(const void *data, size_t size)
{
    pthread_mutex_lock(&frame.mutex);
    if(size > MAX_FRAME)
    {
        fprintf(stderr, "stream_update: frame size %zu exceeds MAX_FRAME %d\n", size, MAX_FRAME);
        pthread_mutex_unlock(&frame.mutex);
        return;
    }
    memcpy(frame.data, data, size);
    frame.version++;
    frame.size = size;
    frame.ready = 1;
    frame.frame_count++;
    pthread_cond_broadcast(&frame.cond);
    pthread_mutex_unlock(&frame.mutex);
}

//获取最新帧数据
int stream_get(uint8_t *buffer, size_t *size, uint32_t *version, uint32_t *frame_count)
{
    if(!buffer || !size || !version)    // 【修复】入口判空，避免调用方少传参数时写空指针
    {
        fprintf(stderr, "stream_get: NULL argument\n");
        return -1;
    }
    pthread_mutex_lock(&frame.mutex);
    while(!frame.ready)
    {
        // 【修复】原来这里是无条件死等：主线程退出时（stream_destroy 后又销毁了 mutex/cond）
        // 还阻塞在 cond_wait 的线程会永远醒不来，属于 use-after-destroy
        if(frame.stop)
        {
            pthread_mutex_unlock(&frame.mutex);
            return -1;
        }
        pthread_cond_wait(&frame.cond, &frame.mutex);
    }
    while(frame.version == *version)
    {
        if(frame.stop)      // 【修复】同上：要求退出时立即返回，让 HTTP 线程结束
        {
            pthread_mutex_unlock(&frame.mutex);
            return -1;
        }
        pthread_cond_wait(&frame.cond, &frame.mutex);
    }
    size_t copy_size = frame.size < MAX_FRAME ? frame.size : MAX_FRAME;
    memcpy(buffer, frame.data, copy_size);
    *size = copy_size;
    *version = frame.version;
    if(frame_count)         // 【修复】原来无条件解引用：调用方不需要帧计数时只能传 NULL，这里容忍 NULL
    {
        *frame_count = frame.frame_count;
    }
    pthread_mutex_unlock(&frame.mutex);
    return 0;
}

//销毁流
void stream_destroy()
{
    pthread_mutex_lock(&frame.mutex);
    frame.ready = 0;
    frame.stop = 1;         // 【修复】置退出标志，配合 stream_get 里的判断唤醒所有等待线程
    pthread_cond_broadcast(&frame.cond);
    pthread_mutex_unlock(&frame.mutex);

    pthread_mutex_destroy(&frame.mutex);
    pthread_cond_destroy(&frame.cond);
}

//截图
int stream_save_snapshot(const char *filename)
{
    pthread_mutex_lock(&frame.mutex);
    if(!frame.ready)
    {
        fprintf(stderr, "stream_save_snapshot: no frame available\n");
        pthread_mutex_unlock(&frame.mutex);
        return -1;
    }
    FILE *fp = fopen(filename, "wb");
    if(!fp)
    {
        perror("fopen");
        pthread_mutex_unlock(&frame.mutex);
        return -1;
    }
    size_t written = fwrite(frame.data, 1, frame.size, fp);
    fclose(fp);
    if(written != frame.size)
    {
        fprintf(stderr, "stream_save_snapshot: failed to write complete frame to %s\n", filename);
        pthread_mutex_unlock(&frame.mutex);
        return -1;
    }
    pthread_mutex_unlock(&frame.mutex);
    return 0;
}

uint64_t stream_frame_count()
{
    pthread_mutex_lock(&frame.mutex);
    uint64_t count = frame.frame_count;
    pthread_mutex_unlock(&frame.mutex);
    return count;
}
