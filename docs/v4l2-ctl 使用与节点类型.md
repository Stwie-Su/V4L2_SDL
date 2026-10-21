---
title: v4l2-ctl 使用与节点类型
tags: [v4l2, linux, camera, notes]
date: 2026-10-20
---

# V4L2 知识总结

## 一、v4l2-ctl 基础用法

`v4l2-ctl` 来自 `v4l-utils` 包（`sudo apt install v4l-utils`），是查询/测试 V4L2 设备的事实标准命令行工具。

### 最常用的五条

```bash
v4l2-ctl --list-devices                      # 列出所有视频设备及其 USB 拓扑
v4l2-ctl -d /dev/video0 --all                # 打开设备并打印全部能力信息
v4l2-ctl -d /dev/video0 --list-formats-ext   # 列出支持的格式/分辨率/帧率
v4l2-ctl -d /dev/video0 --get-fmt-video      # 只取当前格式
v4l2-ctl -d /dev/video0 --get-parm           # 只取当前帧率参数
```

### 测试采集能否工作（会真的取流）

```bash
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=30
# 输出一行帧率统计：
# <<<<<<<<<<<< 29.99 fps
```

### 调试内核日志（排查选帧失败/带宽不足）

```bash
v4l2-ctl -d /dev/video0 --log-status
# 或内核侧：dmesg | grep -i uvc
```

### 常用参数

- `-d, --device` 指定设备节点
- `-f, --format` / `-s, --size` / `-p, --pixel-format` 设定采集参数
- `--all` 打全部信息，`--help` 看完整列表

### `--all` 输出里看哪几段（这是判断节点类型的关键）

```
Driver Info:
    Capabilities      : 0x84a00001      ← 设备整体能力（所有节点都一样，别看这个）
        Video Capture
        Metadata Capture
    Device Caps       : 0x04200001      ← 这个节点自己的能力（关键！）
        Video Capture
        Streaming
Format Video Capture:                  ← 真采集节点才有这一段
    Width/Height      : 640/480
    Pixel Format      : 'MJPG'
Format Metadata Capture:               ← 元数据节点才有这一段
    Sample Format     : 'UVCH'
```

---

## 二、如何判断采集节点 vs 元数据节点

### 规则 1（最可靠）：看 `Device Caps`，不要看 `Capabilities`

- `Device Caps` 里含 **`Video Capture`** → **采集节点**
- `Device Caps` 里只有 **`Metadata Capture`**、没有 `Video Capture` → **元数据节点**

`Capabilities` 是设备层面合并后的能力，两个节点输出完全相同，用它判断必然出错。

### 规则 2：看 `Format` 段的名字

- 出现 `Format Video Capture:`（带 Width/Height、Pixel Format）→ 采集节点
- 出现 `Format Metadata Capture:`（带 Sample Format: 'UVCH'）→ 元数据节点

### 规则 3：试列举格式

```bash
v4l2-ctl -d /dev/video1 --list-formats-ext
```

- 输出为空 → 元数据节点（没有可选的视频格式）
- 列出 MJPG / YUYV 等 → 采集节点

### 规则 4：看 media 拓扑（辅助）

```bash
media-ctl -p
```

两个节点在同一个 media 图里，能看出各自挂在哪个 entity 上。

### 本机实际结果

| | /dev/video0 | /dev/video1 |
|---|---|---|
| Device Caps | **Video Capture**, Streaming | **Metadata Capture**, Streaming |
| Format 段 | `Format Video Capture:` 640/480 MJPG | `Format Metadata Capture:` 'UVCH' |
| 结论 | **采集节点** | **元数据节点** |

> ⚠️ **节点编号不固定**：换 USB 口、重新插拔、换摄像头，`video0`/`video1` 的角色可能互换。启动前用 `v4l2-ctl --list-devices` 确认，或让程序遍历 `/dev/video*` 自动挑出 `Device Caps` 含 `Video Capture` 的那个。本项目 `v4l2_probe` 就是干这件事的。

---

## 三、元数据节点 / 采集节点分别是什么

### 采集节点（Video Capture Node）

普通意义上的摄像头设备节点。你对它做 `VIDIOC_STREAMON`，读出来的是**一帧一帧的图像**（MJPG 压缩流或 YUYV 裸像素）。所有正常应用（拍照、录像、推流、显示）都只用它。

对应 V4L2 能力位：`V4L2_CAP_VIDEO_CAPTURE`。

### 元数据节点（Metadata Capture Node）

**UVC 协议特有的东西**。USB 摄像头在传输图像的同时，会在 USB 数据包的包头里附带一点额外信息，驱动（`uvcvideo`）把它单独解析出来，暴露成第二个 `/dev/videoN` 节点。

典型内容：

- **精确的 PTS（presentation timestamp）**：该帧真正到达主机的时间戳
- 帧序号 / 帧计数
- 错误标志（丢帧、带宽不足等）

用途：需要精确帧级打时间戳的场景——多摄像头同步、低延迟推流、帧级时序分析。普通显示/录像应用**完全不需要**它；对它做 streamon 拿不到任何图像。

对应 V4L2 能力位：`V4L2_CAP_METADATA_CAPTURE`（内核 3.15 之后引入，配合 UVC 的 metadata 流拆分）。

### 非 UVC 摄像头会有吗？

**基本不会有。** 理由：

- `V4L2_CAP_METADATA_CAPTURE` 这一整套机制是围绕 **UVC 规范里的 Metadata stream（VS_METADATA）** 设计的，`uvcvideo` 是 Linux 上**唯一**实现"独立 metadata 采集节点"的驱动。
- 其他驱动（`bttv`、`cx238xx`、`stwebcam`、`sn9c20x` 等老式摄像头、以及绝大多数厂商自研驱动）都只提供**一个**采集节点。
- 少数平台驱动（如 Rockchip `rkisp`、NXP `imx` 系列）确实有 "metadata" 概念，但那是 **V4L2 sub-device 层的 sensor 曝光/增益元数据**（通过 `VIDIOC_SUBDEV_G_META` 读），**不是** `/dev/videoN` 上独立的 `Metadata Capture` 节点，跟 UVC 那个完全是两回事。

**结论**：看到 `Device Caps` 里只有 `Metadata Capture`，基本就说明这是个 UVC 摄像头（`uvcvideo` 驱动）的附属节点。非 UVC 设备你只会看到采集节点。

---

## 四、速查小结

```bash
v4l2-ctl --list-devices                        # 1. 有哪些设备
v4l2-ctl -d /dev/videoX --all                  # 2. 看 Device Caps
                                             #    含 Video Capture → 采集节点
                                             #    只含 Metadata Capture → 元数据节点
v4l2-ctl -d /dev/videoX --list-formats-ext    # 3. 确认格式/分辨率/帧率
v4l2-ctl -d /dev/videoX --stream-mmap --stream-count=30   # 4. 实测能出流
```

对应本项目：

```bash
./build/v4l2_probe /dev/video0   # 项目内的探测工具
```

注意 `src/main_display.c` 顶部设备宏写死的是哪个节点，重插拔后要重新确认。
