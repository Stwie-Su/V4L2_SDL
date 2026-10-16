/*
 * v4l2_capture.c —— mmap 方式采集的实现（本项目的核心，手写目标）
 *
 * 【V4L2 mmap 采集完整流程】——每一步对应一个 ioctl
 *
 *   ① open("/dev/video0")                   打开设备，拿到 fd
 *   ② VIDIOC_QUERYCAP                       查能力：是不是采集设备、支不支持 streaming
 *   ③ VIDIOC_S_FMT                          设定格式（像素格式 + 分辨率）；设完要回读确认
 *   ④ VIDIOC_REQBUFS                        向驱动申请 N 个缓冲
 *   ⑤ VIDIOC_QUERYBUF + mmap   （×N）        查每个缓冲的信息，映射到本进程
 *   ⑥ VIDIOC_QBUF              （×N）        把空缓冲交给驱动（入队）
 *   ⑦ VIDIOC_STREAMON                       开始出帧
 *   ⑧ 循环：
 *        VIDIOC_DQBUF                        取一个已填好的缓冲（出队）；没帧时睡觉
 *        ...用户读数据...
 *        VIDIOC_QBUF                         还回去，驱动继续往里面填
 *   ⑨ 结束：
 *        VIDIOC_STREAMOFF                    停流
 *        munmap + close                      释放
 *
 * 【为什么要有多个缓冲】
 *   一个缓冲一旦交给驱动（QBUF），在它填完之前用户不能碰；
 *   取回来（DQBUF）正在处理时，驱动又没地方写新帧。
 *   多个缓冲轮流用：驱动在填 1 号时，用户可以处理 2 号，互不打架。
 *   这就是"缓冲队列"存在的意义——摄像头流程里的标准设计。
 */

#include "v4l2_capture.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#define MAX_BUFS 8

/* 模块内部状态（单例：一个进程只用一个摄像头） */
static struct {
    int      fd;                 /* 设备文件描述符 */
    int      streaming;          /* 是否已 STREAMON */
    uint32_t width, height;      /* 实际协商到的尺寸 */
    uint32_t nbufs;              /* 实际拿到的缓冲个数 */
    void    *addr[MAX_BUFS];     /* 每个缓冲映射到用户态的起始地址 */
    size_t   length[MAX_BUFS];   /* 每个缓冲的字节长度 */
} g;

/* 所有 ioctl 都走这里：统一处理被信号打断的情况（EINTR 时重试） */
static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

int v4l2_capture_start(const char *dev, uint32_t width, uint32_t height, uint32_t buf_count)
{
    if (g.streaming)
        return -1;                       /* 已在运行 */

    memset(&g, 0, sizeof(g));
    g.fd = -1;
    if (buf_count < 2 || buf_count > MAX_BUFS)
        buf_count = 4;

    /* ① 打开设备 */
    g.fd = open(dev, O_RDWR);
    if (g.fd < 0) {
        fprintf(stderr, "[capture] open %s 失败: %s\n", dev, strerror(errno));
        return -1;
    }

    /* ② 查能力：必须是"视频采集 + 流式（mmap）"设备 */
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(g.fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "[capture] VIDIOC_QUERYCAP 失败: %s\n", strerror(errno));
        goto fail;
    }
    /* 有 device_caps 时以它为准（表示"本节点"的能力） */
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                               : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "[capture] %s 不是可流式采集的设备（caps=0x%08x）\n", dev, caps);
        goto fail;
    }

    /* ③ 设定格式：像素格式写死 MJPG（探测结论），尺寸用参数 */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width;
    fmt.fmt.pix.height      = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;
    if (xioctl(g.fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "[capture] VIDIOC_S_FMT 失败: %s\n", strerror(errno));
        goto fail;
    }
    /* 关键：驱动可能"讲价"（不采纳请求的格式）。不回读确认，
     * 后面就会把 MJPG 数据当别的格式解，画面花屏还找不到原因。 */
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "[capture] 驱动不支持 MJPG，实际给了 0x%08x\n",
                fmt.fmt.pix.pixelformat);
        goto fail;
    }
    g.width  = fmt.fmt.pix.width;
    g.height = fmt.fmt.pix.height;
    printf("[capture] 格式: MJPG %ux%u\n", g.width, g.height);

    /* ④ 申请缓冲：告诉驱动"我要 mmap 方式，先给我 N 个空缓冲" */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = buf_count;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(g.fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "[capture] VIDIOC_REQBUFS 失败: %s\n", strerror(errno));
        goto fail;
    }
    if (req.count < 2) {
        fprintf(stderr, "[capture] 驱动只给了 %u 个缓冲，太少\n", req.count);
        goto fail;
    }
    g.nbufs = req.count;

    /* ⑤ 逐个查缓冲并映射到本进程 */
    for (uint32_t i = 0; i < g.nbufs; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(g.fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "[capture] VIDIOC_QUERYBUF[%u] 失败: %s\n", i, strerror(errno));
            goto fail;
        }
        g.length[i] = buf.length;
        void *p = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, g.fd, buf.m.offset);
        if (p == MAP_FAILED) {
            fprintf(stderr, "[capture] mmap[%u] 失败: %s\n", i, strerror(errno));
            goto fail;
        }
        g.addr[i] = p;
    }

    /* ⑥ 把所有空缓冲交给驱动（入队） */
    for (uint32_t i = 0; i < g.nbufs; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(g.fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "[capture] VIDIOC_QBUF[%u] 失败: %s\n", i, strerror(errno));
            goto fail;
        }
    }

    /* ⑦ 开流：从这一刻起驱动开始往队列里的缓冲填帧 */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(g.fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "[capture] VIDIOC_STREAMON 失败: %s\n", strerror(errno));
        goto fail;
    }
    g.streaming = 1;
    printf("[capture] 采集已启动：%u 个缓冲\n", g.nbufs);
    return 0;

fail:
    v4l2_capture_stop();                 /* 失败路径统一交给 stop 清理（幂等） */
    return -1;
}

int v4l2_capture_get(const uint8_t **frame, size_t *len)
{
    if (!g.streaming)
        return -1;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    /* 出队：没帧时本进程会在这里睡着，驱动填好一帧后唤醒（阻塞模式） */
    if (xioctl(g.fd, VIDIOC_DQBUF, &buf) < 0) {
        fprintf(stderr, "[capture] VIDIOC_DQBUF 失败: %s\n", strerror(errno));
        return -1;
    }
    *frame = g.addr[buf.index];
    *len   = buf.bytesused;              /* 这一帧的实际字节数（MJPG 即 JPEG 数据长度） */
    return 0;
}

int v4l2_capture_done(const uint8_t *frame)
{
    if (!g.streaming)
        return -1;

    /* 用指针反查它属于哪个缓冲（get 给出去的就是这些地址之一） */
    uint32_t index = g.nbufs;
    for (uint32_t i = 0; i < g.nbufs; i++) {
        if (g.addr[i] == frame) {
            index = i;
            break;
        }
    }
    if (index == g.nbufs) {
        fprintf(stderr, "[capture] done：指针不认识（不是 get 给过的地址）\n");
        return -1;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = index;
    if (xioctl(g.fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "[capture] VIDIOC_QBUF(还帧) 失败: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void v4l2_capture_stop(void)
{
    if (g.fd < 0)
        return;

    if (g.streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(g.fd, VIDIOC_STREAMOFF, &type);
        g.streaming = 0;
    }
    for (uint32_t i = 0; i < g.nbufs; i++) {
        if (g.addr[i]) {
            munmap(g.addr[i], g.length[i]);
            g.addr[i] = NULL;
        }
    }
    close(g.fd);
    g.fd = -1;
    g.nbufs = 0;
}

uint32_t v4l2_capture_width(void)
{
    return g.width;
}

uint32_t v4l2_capture_height(void)
{
    return g.height;
}
