/*
 * main_display.c —— 摄像头实时显示（最简版）
 *
 * 数据流（一帧走完全程）：
 *   V4L2 采集层（mmap 直接读到一帧 MJPG 字节流）
 *     → stb_image 把 MJPG（就是 JPEG）解码成 RGB888
 *       → SDL2 上传纹理 → 窗口显示
 *
 * 参数全部"写死"在下面 —— 来源：./build/v4l2_probe 的探测结果。
 * 换相机 / 换分辨率时，改下面三个常量即可。
 *
 * 操作：窗口里按 ESC 或 q 退出，点窗口关闭按钮也行。
 *      运行时每秒打印一行统计：帧率 + 本进程 CPU 占用。
 */

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "stb_image.h"
#include "v4l2_capture.h"

/* ---------- 写死的参数（来自 v4l2_probe 探测报告） ---------- */
#define CAM_DEV   "/dev/video0"
#define CAM_W     864
#define CAM_H     480
#define CAM_BUFS  4               /* 驱动缓冲个数 */

/* ---------- 小工具：取当前时间（毫秒，单调时钟） ---------- */
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ---------- 小工具：读本进程已消耗的 CPU 时间（单位：时钟滴答） ----------
 * /proc/self/stat 第 14、15 个字段是 utime + stime。
 * 第 2 个字段（进程名）带括号且可能含空格，所以先从最后一个 ')' 之后开始数。 */
static long proc_cpu_ticks(void)
{
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f)
        return 0;
    char buf[512];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);

    char *p = strrchr(buf, ')');
    long utime = 0, stime = 0;
    if (p)
        sscanf(p + 2,
               "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld",
               &utime, &stime);
    return utime + stime;
}

/* ---------- 每秒打印一次：帧率 + 本进程 CPU 占用 ---------- */
static void stats_tick(int *frames, double *t_begin, long *cpu_begin)
{
    (*frames)++;
    double t_now = now_ms();
    double dt = t_now - *t_begin;
    if (dt < 1000.0)
        return;

    long  cpu_now = proc_cpu_ticks();
    long  clk     = sysconf(_SC_CLK_TCK);          /* 每秒多少个滴答 */
    double cpu_pct = (cpu_now - *cpu_begin) * 100.0 / clk / (dt / 1000.0);

    printf("[stats] %.1f fps | 本进程 CPU 占用 %.1f%%\n",
           *frames * 1000.0 / dt, cpu_pct);

    *frames    = 0;
    *t_begin   = t_now;
    *cpu_begin = cpu_now;
}

int main(void)
{
    /* 日志按行刷新：stdout 重定向到文件时默认是 4KB 块缓冲，
     * 不设这一行，日志会攒在内存里看不到（前台终端不受影响）。 */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* ---- 1. 启动 V4L2 采集 ---- */
    if (v4l2_capture_start(CAM_DEV, CAM_W, CAM_H, CAM_BUFS) != 0)
        return 1;
    uint32_t w = v4l2_capture_width();
    uint32_t h = v4l2_capture_height();

    /* ---- 2. 初始化 SDL 窗口 ---- */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init 失败: %s\n", SDL_GetError());
        v4l2_capture_stop();
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("V4L2 preview  (ESC quit)",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       (int)w, (int)h, 0);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    /* 像素格式用 RGB24：stb 解码输出就是 RGB888（每像素 3 字节 R,G,B），零转换直接上传 */
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
                                               SDL_TEXTUREACCESS_STREAMING,
                                               (int)w, (int)h) : NULL;
    if (!win || !ren || !tex) {
        fprintf(stderr, "SDL 窗口/渲染器/纹理 创建失败: %s\n", SDL_GetError());
        if (tex) SDL_DestroyTexture(tex);
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        v4l2_capture_stop();
        return 1;
    }

    /* ---- 3. 主循环：取帧 → 解码 → 显示 ---- */
    int    quit      = 0;
    int    frames    = 0;
    double t_begin   = now_ms();
    long   cpu_begin = proc_cpu_ticks();

    while (!quit) {
        /* 3.1 处理窗口事件（退出） */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_KEYDOWN &&
                 (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q)))
                quit = 1;
        }
        if (quit)
            break;

        /* 3.2 取一帧 MJPG（没帧就阻塞等待） */
        const uint8_t *jpg;
        size_t jpg_len;
        if (v4l2_capture_get(&jpg, &jpg_len) != 0)
            break;

        /* 3.3 解码 MJPG → RGB888 */
        int dw, dh;
        unsigned char *rgb = stbi_load_from_memory(jpg, (int)jpg_len, &dw, &dh, NULL, 3);
        v4l2_capture_done(jpg);      /* 数据到手就还回缓冲，让驱动继续往里填 */

        if (!rgb) {
            printf("[display] 第 %d 帧解码失败，跳过\n", frames);
            continue;
        }

        /* 3.4 上传纹理并显示整帧 */
        SDL_UpdateTexture(tex, NULL, rgb, dw * 3);
        stbi_image_free(rgb);

        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        stats_tick(&frames, &t_begin, &cpu_begin);
    }

    /* ---- 4. 收尾 ---- */
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    v4l2_capture_stop();
    printf("[display] 已退出\n");
    return 0;
}
