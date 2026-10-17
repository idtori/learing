# 4vl2视频流摄像头
① 打开 /dev/video0 ② VIDIOC_S_FMT 设格式（YUYV → MJPEG 480x640）③ 申请缓冲并 mmap ④ VIDIOC_QBUF 缓冲入队 ⑤ VIDIOC_STREAMON 开流 ⑥ 采集线程 poll + VIDIOC_DQBUF 取出一帧、分片编码写文件 ⑦ HTTP 线程送给浏览器

<img width="624" height="433" alt="屏幕截图 2026-10-17 173635" src="https://github.com/user-attachments/assets/8f5fafd0-d49b-47f5-9ab9-0859bfc947be" />

平均帧率为20帧左右，大小640x480

接入usb摄像头后
在powershell绑定摄像头:
usbpid bind --busid 1-1
usbpid attach --wsl --busid 1-1

wsl乌邦图终端:
./server
