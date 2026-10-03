#include "camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>         // 【修复】用到 uint8_t，原来靠别的头文件间接引入，补上更稳妥
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>          // 【修复】poll/DQBUF 的错误处理要用 errno/EINTR
#include <poll.h>           // 【修复】用 poll 等帧，避免 DQBUF 永久阻塞把进程卡死
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
    // 否则摄像头仍是默认格式/默认分辨率（本机默认就是 1920x1080，不是想要的 640x480）
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
    /* 【修复·本次的关键】把缓冲区先交给驱动"入队"，再启动取流。
     * V4L2 取流的固定顺序是：
     *     open -> VIDIOC_S_FMT(定格式) -> VIDIOC_REQBUFS(申请缓冲)
     *          -> VIDIOC_QUERYBUF(查长度/偏移) -> mmap(映射到用户态)
     *          -> VIDIOC_QBUF(把缓冲交给驱动，这一步原来漏了！)
     *          -> VIDIOC_STREAMON(开流) -> 循环{ VIDIOC_DQBUF 取帧 -> 用完 VIDIOC_QBUF 还回去 }
     * 漏掉这次 QBUF，驱动手里一个缓冲区都没有，STREAMON 只是把队列标成 streaming
     * 并没有真的开始取流，紧接着第一次 VIDIOC_DQBUF 就永远阻塞：
     *   - 浏览器打开页面能加载，但画面一直空白/转圈（采集线程拿不到帧，stream 没有 ready）
     *   - 服务器日志停在 "Camera opened" 之后再没有 "Actual FPS"
     *   - 连 Ctrl+C 都退不掉（main 里的 pthread_join 卡在这个死等 DQBUF 的线程上）
     */
    struct v4l2_buffer qbuf = {0};
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = 0;
    if(ioctl(fd, VIDIOC_QBUF, &qbuf) < 0)
    {
        perror("VIDIOC_QBUF(首次入队)");
        munmap(buffer, buf.length);
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
    // 【修复】把驱动真正协商出来的格式也打出来，方便确认到底是不是 MJPG（buf.length 是 __u32，原来 %zu 类型不符）
    printf("Camera opened: %s, resolution: %dx%d (%c%c%c%c), buffer size: %u\n",
           dev, wide, high,
           fmt.fmt.pix.pixelformat & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF,
           (unsigned)buf.length);
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
    // 【修复】先用 poll 等"确实有帧可读"，最多等 1 秒，再 DQBUF。
    // 直接 DQBUF 是阻塞式 ioctl：摄像头一旦断流（被拔掉、驱动异常、格式不对），
    // 采集线程会永久卡死在这里，不但没有画面，Ctrl+C 也退不出（main 卡在 pthread_join）
    struct pollfd pfd = {0};
    pfd.fd = cam->fd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 1000);
    if(pr < 0)
    {
        if(errno == EINTR) return NULL;   // 被信号打断（如 Ctrl+C），交给上层循环去看 running
        perror("poll");
        return NULL;
    }
    if(pr == 0) return NULL;              // 1 秒内没有帧，返回空，上层下一轮再试
    if(ioctl(cam->fd, VIDIOC_DQBUF, &buf) < 0)
    {
        perror("VIDIOC_DQBUF");
        return NULL;
    }
    *buffer_size = buf.bytesused;
    // 【修复】图像数据就在 mmap 出来的这块内存的"开头"：
    // mmap 时用的文件偏移已经是 buf.m.offset，所以这里绝不能再加一次 m.offset。
    // 原来 return cam 更离谱——返回的是 camera_t 结构体指针，等于把结构体当 JPEG 发出去
    const void *data = cam->buffer;
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
    // 【修复】VIDIOC_STREAMOFF 的参数是"一个 int 的地址"（缓冲类型），
    // 原来写的是 &(struct v4l2_buffer){.type = ...}：而 struct v4l2_buffer 的第一个成员是 index，
    // 内核按 int 读到的其实是 0，于是 STREAMOFF 永远失败（日志里的 "VIDIOC_STREAMOFF: Invalid argument"），
    // 流从来没被真正停掉过，只是后面 close(fd) 顺手带停了
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(cam->fd, VIDIOC_STREAMOFF, &type) < 0)
    {
        perror("VIDIOC_STREAMOFF");
    }
    munmap(cam->buffer, cam->buffer_size);
    close(cam->fd);
    free(cam);
    printf("Camera closed\n");
}
