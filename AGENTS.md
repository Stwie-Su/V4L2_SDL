# AGENTS.md

V4L2 摄像头学习项目（Ubuntu / C11 / CMake / SDL2）。注释、文档、提交信息都是中文，保持一致。没有测试、CI、lint。

## 构建与运行

- 依赖 libsdl2-dev（`sudo apt install libsdl2-dev`）。CMake 里 SDL2 是 `REQUIRED`，缺了连不用 SDL 的 v4l2_probe 也配置不了。
- `cmake -B build && cmake --build build -j`（`build/` 已 gitignore，无其它生成步骤）。
- 若 `cmake -B build` 报缓存路径与源码路径不匹配（本目录被移动过），先 `rm -rf build` 再重新配置。
- 运行需要真实摄像头 + 图形环境：
  - `./build/v4l2_probe [设备节点]` —— 先探测能力（默认 /dev/video0）。
  - `./build/v4l2_display` —— SSH 里跑要先 `export DISPLAY=:1`；ESC/q 退出，每秒打印 `[stats]` 帧率和进程 CPU。

## 代码事实（容易踩的点）

- `src/v4l2_capture.c` 是核心（mmap 采集九步流程），单例：一个进程只能开一路；`v4l2_capture_get` 拿到的指针必须原样传给 `v4l2_capture_done` 还回。
- 采集格式写死 MJPG（S_FMT 后回读确认，驱动不给 MJPG 直接失败）。本机相机只支持 MJPG，且不支持 read()，只能 mmap。
- 设备 / 分辨率 / 缓冲数写死在 `src/main_display.c` 顶部 4 个宏，改完要重新构建。
- `src/stb_image.h` 是第三方 vendored 单文件库，不要修改；实现只在 `src/stb_image_impl.c` 编译一次（STBI_ONLY_JPEG 裁剪），CMake 对那个文件单独 `-w` 压警告，自己代码保持 `-Wall -Wextra` 干净。
- `docs/性能优化设计.md` 是尚未实现的优化设计（丢旧帧 / 多线程 / libjpeg-turbo / YUV 纹理等），不要当成现有实现；各项可独立加入。
- 本机相机（全速 USB 模组）实测上限：864×480 ≈ 5.8 fps，320×240 ≈ 17.7 fps（与 v4l2-ctl 结果一致）。高分辨率帧率低是硬件链路瓶颈，不是程序 bug，不要盲目"优化"帧率。

## 验证

没有单元测试。最低标准：`cmake --build build -j` 通过（C11，-Wall -Wextra 应无警告）；有摄像头时跑 probe 确认能力、跑 display 看 `[stats]` 是否正常。
