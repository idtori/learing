# learing
① 打开 /dev/video0 ② VIDIOC_S_FMT 设格式（YUYV → MJPEG 480x640）③ 申请缓冲并 mmap ④ VIDIOC_QBUF 缓冲入队 ⑤ VIDIOC_STREAMON 开流 ⑥ 采集线程 poll + VIDIOC_DQBUF 取出一帧、分片编码写文件 ⑦ HTTP 线程送给浏览器
