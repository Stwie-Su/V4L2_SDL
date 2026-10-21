/*
 * main_display.c —— 摄像头实时显示（最简版）
 * 流程：V4L2阻塞取帧(MJPG) → stb软解(RGB) → SDL上传纹理并显示
 */

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "stb_image.h"
#include "v4l2_capture.h"

/* 写死的参数：设备、分辨率、缓冲数 */
#define CAM_DEV   "/dev/video0"
#define CAM_W     1280
#define CAM_H     720
#define CAM_BUFS  4

/* 取单调时钟毫秒（算帧率用，防系统时间跳变） */
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* 读进程消耗的CPU滴答数（utime + stime），从最后一个 ')' 后开始解析 */
static long proc_cpu_ticks(void)
{
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return 0;
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

/* 统计打印：满1秒算一次fps和CPU%，然后清零重计 */
static void stats_tick(int *frames, double *t_begin, long *cpu_begin)
{
    (*frames)++;
    double t_now = now_ms();
    double dt = t_now - *t_begin;
    if (dt < 1000.0) return;

    long  cpu_now = proc_cpu_ticks();
    long  clk     = sysconf(_SC_CLK_TCK);
    double cpu_pct = (cpu_now - *cpu_begin) * 100.0 / clk / (dt / 1000.0);

    printf("[stats] %.1f fps | 本进程 CPU 占用 %.1f%%\n",
           *frames * 1000.0 / dt, cpu_pct);

    *frames    = 0;
    *t_begin   = t_now;
    *cpu_begin = cpu_now;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0); /* 日志行缓冲，重定向到文件也能实时看 */

    /* ---- 1. 启动 V4L2 采集 ---- */
    if (v4l2_capture_start(CAM_DEV, CAM_W, CAM_H, CAM_BUFS) != 0)
        return 1;
    uint32_t w = v4l2_capture_width();   /* 必须用驱动实际协商的宽高 */
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
    /* 渲染器不加 VSYNC，防止 Present 阻塞等屏幕刷新 */
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    /* 纹理选 RGB24，和 stb 输出的格式完全一致，上传时零转换 */
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
                                               SDL_TEXTUREACCESS_STREAMING,
                                               (int)w, (int)h) : NULL;
    if (!win || !ren || !tex) {
        fprintf(stderr, "SDL 创建失败: %s\n", SDL_GetError());
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
        /* 【① 非阻塞探路】 */
        /* 瞄一眼有没有按ESC或点关闭。没事件立刻返回0，绝不逗留、不耽误下面等帧 */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) 
        {
            if (ev.type == SDL_QUIT || 
               (ev.type == SDL_KEYDOWN && 
               (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q)))
                quit = 1;
        }
        /* 必须在进阻塞等帧前退出，否则按了ESC还得傻等一帧才退 */
        if (quit)
            break;

        /* 【② 阻塞等帧】 */
        /* 内部是阻塞 DQBUF。没数据就睡死在这，CPU占用为0。这里决定了循环快慢 */
        const uint8_t *jpg;
        size_t jpg_len;
        if (v4l2_capture_get(&jpg, &jpg_len) != 0)
            break;

        /* 【③ 解码处理】 */
        /* stb 在 CPU 上把 MJPG 解成 RGB888，返回新 malloc 的内存 */
        int dw, dh;
        unsigned char *rgb = stbi_load_from_memory(jpg, (int)jpg_len, &dw, &dh, NULL, 3);
        
        /* 重点：解码完必须立刻归还驱动 buffer，让底层去填下一帧 */
        v4l2_capture_done(jpg); 

        if (!rgb) {
            printf("[display] 第 %d 帧解码失败，跳过\n", frames);
            continue; /* buffer 已归还，直接跳过这次显示 */
        }

        /* 【④ 显示（三次搬动）】 */
        /* 1. 把 CPU 内存丢进显存（拷贝），pitch = dw*3 */
        SDL_UpdateTexture(tex, NULL, rgb, dw * 3);
        /* 拷贝完就能释放 stb 解出来的内存了 */
        stbi_image_free(rgb);

        /* 2. 清后台 + 画图 + 翻页给用户看 */
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        /* 3. 统计 */
        stats_tick(&frames, &t_begin, &cpu_begin);
    }

    /* ---- 4. 收尾：逆序销毁 ---- */
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    v4l2_capture_stop();
    printf("[display] 已退出\n");
    return 0;
}