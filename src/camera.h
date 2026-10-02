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
// 【修复】返回类型由 camera_t * 改为 const void *：
// 原来函数内部 return cam（返回结构体指针），而 main.c 把返回值当 JPEG 数据发给 stream，
// 于是一路 memcpy 的都是 camera_t 结构体内容，根本不是图像数据
const void *camera_capture(camera_t *cam, size_t *buffer_size);

//关闭摄像头
void camera_close(camera_t *cam);

#endif
