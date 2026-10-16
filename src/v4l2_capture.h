#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

/*
 * v4l2_capture —— V4L2 摄像头采集模块（mmap 方式）
 *
 * 整个模块就干一件事：
 *   打开摄像头 → 申请驱动缓冲并 mmap 到本进程 → 开会出帧；
 *   之后循环"取一帧 → 用完 → 还回去"。
 *
 * 为什么用 mmap 而不是 read()：
 *   read() 是把内核里的帧"拷贝一份"给用户（有一次内存拷贝），
 *   而且很多摄像头（UVC 驱动）根本不支持 read()；
 *   mmap 是把驱动缓冲"直接映射"进本进程的地址空间，
 *   用户读的就是驱动填好的那块内存（零拷贝）。
 *   摄像头这类高吞吐设备，标准做法就是 mmap。
 *
 * 典型用法：
 *   if (v4l2_capture_start("/dev/video0", 864, 480, 4) == 0) {
 *       const uint8_t *frame; size_t len;
 *       while (还在取帧) {
 *           if (v4l2_capture_get(&frame, &len) == 0) {   // 阻塞等一帧
 *               ... 用 frame[0..len) ...
 *               v4l2_capture_done(frame);                // 必须还回，否则缓冲越用越少
 *           }
 *       }
 *       v4l2_capture_stop();
 *   }
 */

#include <stddef.h>
#include <stdint.h>

/* 打开设备并启动采集流。像素格式固定为 MJPG（本相机只支持它）。
 * dev:       设备节点，如 "/dev/video0"
 * width/height: 请求的分辨率（驱动可能不采纳，实际值用下面两个函数查）
 * buf_count: 驱动缓冲个数（2~8，一般 4）
 * 返回 0 成功；-1 失败（原因打印到 stderr）。 */
int v4l2_capture_start(const char *dev, uint32_t width, uint32_t height, uint32_t buf_count);

/* 取一帧（VIDIOC_DQBUF）：阻塞，直到驱动填好一帧。
 * 成功：*frame 指向这帧数据（MJPG 字节流），*len 是帧长度，返回 0。
 * 这块内存属于驱动缓冲，在调用 v4l2_capture_done 还回去之前一直有效。 */
int v4l2_capture_get(const uint8_t **frame, size_t *len);

/* 把取过的帧还回驱动（VIDIOC_QBUF）。
 * 参数必须是 v4l2_capture_get 给过的那个指针。 */
int v4l2_capture_done(const uint8_t *frame);

/* 停止采集并释放全部资源（STREAMOFF → munmap → close）。可重复调用。 */
void v4l2_capture_stop(void);

/* 实际协商到的分辨率（S_FMT 后驱动最终采用的尺寸）。 */
uint32_t v4l2_capture_width(void);
uint32_t v4l2_capture_height(void);

#endif /* V4L2_CAPTURE_H */
