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
    pthread_mutex_lock(&frame.mutex);
    while(!frame.ready)
    {
        pthread_cond_wait(&frame.cond, &frame.mutex);
    }
    while(frame.version == *version)
    {
        pthread_cond_wait(&frame.cond, &frame.mutex);
    }
    size_t copy_size = frame.size < MAX_FRAME ? frame.size : MAX_FRAME;
    memcpy(buffer, frame.data, copy_size);
    *size = copy_size;
    *version = frame.version;
    *frame_count = frame.frame_count;
    pthread_mutex_unlock(&frame.mutex);
    return 0;
}

//销毁流
void stream_destroy()
{
    pthread_mutex_lock(&frame.mutex);
    frame.ready = 0;
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