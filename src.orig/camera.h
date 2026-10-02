#ifndef __CAMERA_H
#define __CAMERA_H
#include <stddef.h>
//定义摄像头结构体
typedef struct 
{
    int fd;
    int wide;
    int high;
    void *buffer;
    size_t buffer_size;
} camera_t;

//打开摄像头
camera_t *camera_open(const char *dev, int wide, int high, int fps);

//获取摄像头图像
camera_t *camera_capture(camera_t *cam, size_t *buffer_size);

//关闭摄像头
void camera_close(camera_t *cam);

#endif