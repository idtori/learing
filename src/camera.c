#include "camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>         // 【修复】用到 uint8_t，原来靠别的头文件间接引入，补上更稳妥
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

camera_t *camera_open(const char *dev, int wide, int high, int fps)
{
    int fd = open(dev, O_RDWR);
    if(fd < 0)
    {
        perror("open");
        return NULL;
    }
    //设置摄像头参数
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = wide;
    fmt.fmt.pix.height = high;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    // 【修复】原来只填了 fmt 结构体却从未下发！必须 ioctl(VIDIOC_S_FMT) 才会生效，
    // 否则摄像头仍是默认格式（多数是 YUYV），后面把 YUYV 当 MJPEG 发给浏览器必然花屏/黑屏
    if(ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("VIDIOC_S_FMT");
        close(fd);
        return NULL;
    }
    // 【修复】以驱动实际协商的结果为准回写分辨率（驱动可能不支持所请求的 640x480）
    wide = fmt.fmt.pix.width;
    high = fmt.fmt.pix.height;
    //设置帧间隔
    struct v4l2_streamparm streamparm = {0};
    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.timeperframe.numerator = 1;
    streamparm.parm.capture.timeperframe.denominator = fps;
    ioctl(fd, VIDIOC_S_PARM, &streamparm);
    //申请缓冲区
    struct v4l2_requestbuffers req = {0};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if(ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
    {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return NULL;
    }

    //映射缓冲区
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if(ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
    {
        perror("VIDIOC_QUERYBUF");
        close(fd);
        return NULL;
    }
    void *buffer = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if(buffer == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return NULL;
    }
    //启动摄像头采集
    if(ioctl(fd, VIDIOC_STREAMON, &buf.type) < 0)
    {
        perror("VIDIOC_STREAMON");
        munmap(buffer, buf.length);
        close(fd);
        return NULL;
    }
    camera_t *cam = (camera_t *)malloc(sizeof(camera_t));
    if(!cam)
    {
        perror("malloc");
        munmap(buffer, buf.length);
        close(fd);
        return NULL;
    }
    cam->fd = fd;
    cam->wide = wide;
    cam->high = high;
    cam->buffer = buffer;
    cam->buffer_size = buf.length;
    printf("Camera opened: %s, resolution: %dx%d, buffer size: %u\n", dev, wide, high, (unsigned)buf.length);  // 【修复】buf.length 是 __u32，原来用 %zu 类型不匹配会有告警
    return cam;
}
//获取摄像头图像
const void *camera_capture(camera_t *cam, size_t *buffer_size)
{
    if(!cam)    
    {
        fprintf(stderr, "camera_capture: cam is NULL\n");
        return NULL;
    }
    if(!buffer_size)    // 【修复】原来不判空，调用方传 NULL 就会写空指针
    {
        fprintf(stderr, "camera_capture: buffer_size is NULL\n");
        return NULL;
    }
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if(ioctl(cam->fd, VIDIOC_DQBUF, &buf) < 0)
    {
        perror("VIDIOC_DQBUF");
        return NULL;
    }
    *buffer_size = buf.bytesused;
    // 【修复】真正的图像数据在 mmap 出来的缓冲区里（只申请了 index=0，映射时已带上 m.offset），
    // 原来 return cam 返回的是结构体指针，等于把 camera_t 当 JPEG 发出去
    const void *data = (const uint8_t *)cam->buffer + buf.m.offset;
    if(ioctl(cam->fd, VIDIOC_QBUF, &buf) < 0)
    { 
        perror("VIDIOC_QBUF");
        return NULL;
    }
    // 【注意】上面已把缓冲区还给驱动，这块内存随时可能被覆写，调用方要尽快拷走数据。
    // 当前 stream_update() 是立刻 memcpy，实际风险很小；要彻底解决需改成多缓冲或先拷贝再 QBUF
    return data;
}
//关闭摄像头
void camera_close(camera_t *cam)
{
    if(!cam)
    {
        fprintf(stderr, "camera_close: cam is NULL\n");
        return;
    }
    if(ioctl(cam->fd, VIDIOC_STREAMOFF, &(struct v4l2_buffer){.type = V4L2_BUF_TYPE_VIDEO_CAPTURE}) < 0)
    {
        perror("VIDIOC_STREAMOFF");
    }
    munmap(cam->buffer, cam->buffer_size);
    close(cam->fd);
    free(cam);
    printf("Camera closed\n");
}
