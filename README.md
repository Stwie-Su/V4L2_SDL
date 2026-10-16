# v4l2_camera —— V4L2 摄像头学习项目

在 Ubuntu 上学习 Linux 摄像头框架 V4L2 的最小完整实践：
**先探测相机能力 → 按探测结果写死参数 → 把画面实时显示到窗口**。

代码按"能手写"的标准写：核心采集流程集中在一个文件（`src/v4l2_capture.c`），
注释讲清每一步为什么。

## 构建

依赖：gcc、cmake、libsdl2-dev（`sudo apt install libsdl2-dev`）

```bash
cmake -B build
cmake --build build -j
```

## 两个程序

### 1. v4l2_probe —— 相机能力探测（先跑这个）

```bash
./build/v4l2_probe               # 默认 /dev/video0
./build/v4l2_probe /dev/video2   # 指定其它设备节点
```

输出：设备信息 / 支持格式 / 分辨率 / 帧率 / 控制项。

### 2. v4l2_display —— 实时显示

```bash
./build/v4l2_display             # 需要图形环境（VM 桌面终端里直接跑；
                                 # SSH 里跑要先 export DISPLAY=:1）
```

窗口里按 ESC 或 q 退出。运行时每秒打印一行：帧率 + 本进程 CPU 占用。

写死的参数在 `src/main_display.c` 顶部（设备节点 / 分辨率 / 缓冲个数），改完重新构建。

## 文件地图

| 文件 | 作用 |
|---|---|
| src/v4l2_probe.c | 探测工具：枚举设备能力的各种 ioctl |
| src/v4l2_capture.h | 采集模块的接口（5 个函数） |
| src/v4l2_capture.c | **核心**：mmap 方式采集的完整流程（手写目标） |
| src/main_display.c | 显示程序：取帧 → 解码 → SDL2 显示 + 统计 |
| src/stb_image.h / src/stb_image_impl.c | 单文件 JPEG 解码库（MJPG 就是 JPEG） |
| docs/性能优化设计.md | 优化路线设计（先设计，后续再实现） |

## 本机相机实测数据（2026-10-15/16，Fuge UVC）

探测结论：

- 只有 MJPG 一种格式；5 档分辨率；驱动声明全部 15 fps
- 不支持 read()（能力位里没有 READWRITE）→ 只能用 mmap 方式采集

各分辨率**实际**帧率（用 v4l2-ctl 和本程序两边测过，结果一致）：

| 分辨率 | 描述符声明 | 实测帧率 |
|---|---|---|
| 864×480 | 15 fps | **5.8 fps** |
| 480×320 | 15 fps | **11.9 fps** |
| 320×240 | 15 fps | **17.7 fps** |

原因：这台相机是**全速 USB 模组**（12 Mbps 链路、Bulk 端点、无高速能力），
大分辨率喂不到 15fps——这是相机/链路的实际能力，不是程序问题
（对照工具 v4l2-ctl 跑出同样数据，且本程序稳定跑满相机产能的 99%）。

想让画面更流畅：改分辨率（比如 320×240 有 17.7fps）；或换一台 USB 高速相机。

## 学什么（对照手写）

1. **v4l2_probe.c** —— V4L2 枚举类 ioctl：
   QUERYCAP 能力位 / ENUM_FMT / ENUM_FRAMESIZES / ENUM_FRAMEINTERVALS /
   G_FMT / G_PARM / QUERYCTRL
2. **v4l2_capture.c（重点，可以照着默写）** —— mmap 采集九步流程：

   ```
   open → QUERYCAP → S_FMT(设完回读确认) → REQBUFS → QUERYBUF + mmap
   → QBUF(全部入队) → STREAMON
   → 循环: DQBUF(取一帧) → 处理 → QBUF(还回去)
   → 结束: STREAMOFF + munmap + close
   ```

3. **main_display.c** —— 一帧的完整旅程：
   DQBUF 拿到 MJPG 字节 → stb 解码成 RGB → SDL 纹理 → 窗口显示

自检问题（能答上说明真懂了）：

- 为什么 S_FMT 之后要回读确认，而不是设完就不管？
- 为什么要有多个缓冲？只给驱动 1 个行不行？
- DQBUF 之后忘记 QBUF，程序会发生什么？
- 这台相机为什么不能用 read()？什么样的设备适合 read()？
