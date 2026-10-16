/*
 * v4l2_probe.c —— 相机能力探测工具
 *
 * 学 V4L2 的第一步：把"查户口"类 ioctl 全部用一遍，看设备到底能干什么。
 * 显示程序里"写死"的参数（格式 / 分辨率 / 帧率），就从本工具的输出来抄。
 *
 * 本工具用到的 ioctl：
 *   VIDIOC_QUERYCAP            设备是谁、能干什么（能力位）
 *   VIDIOC_G_FMT               当前格式
 *   VIDIOC_ENUM_FMT            支持哪些像素格式
 *   VIDIOC_ENUM_FRAMESIZES     每种格式支持哪些分辨率
 *   VIDIOC_ENUM_FRAMEINTERVALS 每个分辨率支持哪些帧率
 *   VIDIOC_G_PARM              当前帧率
 *   VIDIOC_QUERYCTRL           有哪些控制项（亮度、对比度……）
 *
 * 用法：./v4l2_probe [设备节点]     默认 /dev/video0
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>

/* 把 4 字符码（如 'MJPG'）转成可打印字符串 */
static void fourcc_str(uint32_t code, char out[5])
{
    out[0] = (char)(code & 0xff);
    out[1] = (char)((code >> 8) & 0xff);
    out[2] = (char)((code >> 16) & 0xff);
    out[3] = (char)((code >> 24) & 0xff);
    out[4] = '\0';
}

/* 打印能力位：哪几项有、哪几项没有，一眼能看清 */
static void print_caps(uint32_t caps)
{
    printf("    视频采集 VIDEO_CAPTURE      : %s\n", (caps & V4L2_CAP_VIDEO_CAPTURE) ? "有" : "无");
    printf("    视频输出 VIDEO_OUTPUT       : %s\n", (caps & V4L2_CAP_VIDEO_OUTPUT) ? "有" : "无");
    printf("    读写方式 READWRITE          : %s%s\n",
           (caps & V4L2_CAP_READWRITE) ? "有" : "无",
           (caps & V4L2_CAP_READWRITE) ? "（可用 read()）" : "（不能用 read()，要用 mmap 流式方式）");
    printf("    流式方式 STREAMING          : %s%s\n",
           (caps & V4L2_CAP_STREAMING) ? "有" : "无",
           (caps & V4L2_CAP_STREAMING) ? "（可用 mmap / userptr）" : "");
    printf("    扩展像素格式 EXT_PIX_FORMAT : %s\n", (caps & V4L2_CAP_EXT_PIX_FORMAT) ? "有" : "无");
    printf("    device_caps 标志            : %s\n", (caps & V4L2_CAP_DEVICE_CAPS) ? "有" : "无");
}

/* 枚举某个格式下所有分辨率；每个分辨率再列出帧率 */
static void list_sizes_and_intervals(int fd, uint32_t pixelformat)
{
    struct v4l2_frmsizeenum size;
    memset(&size, 0, sizeof(size));
    size.pixel_format = pixelformat;

    for (size.index = 0; ; size.index++) {
        if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) < 0)
            break;                          /* 枚举完（或该格式不报尺寸）就退出 */

        if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
            printf("      （非离散分辨率，本工具不展开）\n");
            break;
        }

        printf("      分辨率 %ux%u\n",
               size.discrete.width, size.discrete.height);

        /* 该分辨率下的帧率 */
        struct v4l2_frmivalenum interval;
        memset(&interval, 0, sizeof(interval));
        interval.pixel_format = pixelformat;
        interval.width  = size.discrete.width;
        interval.height = size.discrete.height;

        for (interval.index = 0; ; interval.index++) {
            if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) < 0)
                break;
            if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
                printf("        帧率：非离散区间\n");
                break;
            }
            /* FrameInterval = numerator/denominator 秒；帧率 = denominator/numerator */
            printf("        帧间隔 %u/%u 秒 = %.1f fps\n",
                   interval.discrete.numerator, interval.discrete.denominator,
                   (double)interval.discrete.denominator / interval.discrete.numerator);
        }
    }
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/video0";

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "打开 %s 失败: %s\n", dev, strerror(errno));
        return 1;
    }

    printf("========================================\n");
    printf(" 相机能力探测报告  %s\n", dev);
    printf("========================================\n\n");

    /* ---- 1. 设备信息 ---- */
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "VIDIOC_QUERYCAP 失败: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("--- 1. 设备信息（VIDIOC_QUERYCAP）---\n");
    printf("  驱动     : %s\n", cap.driver);
    printf("  设备名   : %s\n", cap.card);
    printf("  总线     : %s\n", cap.bus_info);
    printf("  整体能力位 capabilities = 0x%08x\n", cap.capabilities);
    print_caps(cap.capabilities);
    if (cap.capabilities & V4L2_CAP_DEVICE_CAPS) {
        /* device_caps 表示"本节点"的能力，比整体能力更准 */
        printf("  本节点能力位 device_caps = 0x%08x\n", cap.device_caps);
        print_caps(cap.device_caps);
    }
    printf("\n");

    /* ---- 2. 当前格式 ---- */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
        char cc[5];
        fourcc_str(fmt.fmt.pix.pixelformat, cc);
        printf("--- 2. 当前格式（VIDIOC_G_FMT）---\n");
        printf("  %s  %ux%u  每行字节数 %u  图像缓冲大小 %u\n",
               cc, fmt.fmt.pix.width, fmt.fmt.pix.height,
               fmt.fmt.pix.bytesperline, fmt.fmt.pix.sizeimage);
    } else {
        printf("--- 2. 当前格式：读取失败（%s）\n", strerror(errno));
    }
    printf("\n");

    /* ---- 3. 枚举格式 → 分辨率 → 帧率 ---- */
    printf("--- 3. 支持的格式（VIDIOC_ENUM_FMT）---\n");
    struct v4l2_fmtdesc fmtdesc;
    int fmt_count = 0;
    memset(&fmtdesc, 0, sizeof(fmtdesc));      /* 清一次；循环里只递增 index，不能清（会死循环） */
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fmtdesc.index = 0; ; fmtdesc.index++) {
        if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0)
            break;                          /* 枚举完返回失败，正常退出 */

        char cc[5];
        fourcc_str(fmtdesc.pixelformat, cc);
        printf("  [%u] %s —— %s\n", fmtdesc.index, cc, fmtdesc.description);
        list_sizes_and_intervals(fd, fmtdesc.pixelformat);
        fmt_count++;
    }
    if (fmt_count == 0)
        printf("  （该设备不支持视频采集格式）\n");
    printf("\n");

    /* ---- 4. 当前帧率 ---- */
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_PARM, &parm) == 0 &&
        (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
        printf("--- 4. 当前帧率（VIDIOC_G_PARM）---\n");
        printf("  %u/%u 秒 = %.1f fps\n",
               parm.parm.capture.timeperframe.numerator,
               parm.parm.capture.timeperframe.denominator,
               (double)parm.parm.capture.timeperframe.denominator /
               parm.parm.capture.timeperframe.numerator);
        printf("\n");
    }

    /* ---- 5. 控制项 ---- */
    printf("--- 5. 控制项（VIDIOC_QUERYCTRL）---\n");
    struct v4l2_queryctrl qc;
    int ctrl_count = 0;
    memset(&qc, 0, sizeof(qc));
    qc.id = V4L2_CTRL_FLAG_NEXT_CTRL;      /* 新式遍历法：从"下一个控制项"开始找 */
    while (ioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0) {
        if (!(qc.flags & V4L2_CTRL_FLAG_DISABLED)) {
            printf("  [0x%08x] %-28s 取值范围 %d .. %d，步进 %u\n",
                   qc.id, qc.name, qc.minimum, qc.maximum, qc.step);
            ctrl_count++;
        }
        qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL; /* 继续找再下一个 */
    }
    if (ctrl_count == 0)
        printf("  （没有可用的控制项）\n");

    close(fd);
    return 0;
}
