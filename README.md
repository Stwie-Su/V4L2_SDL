# v4l2_camera —— V4L2 面试准备

优化结论与问答见 [V4L2面试优化与QA.md](V4L2面试优化与QA.md)。

## 学习条目

按"面试能讲清"的顺序排列，不按文件顺序。

1. **整体流程与缓冲所有权** —— 九步要能默写，更要能讲清每步为什么存在、失败会怎样 —— `src/v4l2_capture.c`
2. **传输方式四选一** —— read / mmap / userptr / dmabuf 的取舍，零拷贝发生在哪一步 —— `src/v4l2_capture.h`
3. **格式协商** —— S_FMT 是协商不是命令；回读校验；pixelformat / bytesperline / sizeimage 的关系 —— `src/v4l2_capture.c`
4. **缓冲管理** —— REQBUFS / QBUF / DQBUF；队列深度与延迟、丢帧的关系 —— `src/v4l2_capture.c`
5. **时间与丢帧检测** —— timestamp 的时钟源、sequence 缺口、帧周期统计 —— `src/main_display.c`
6. **等待模型** —— 阻塞 DQBUF 与 poll + 非阻塞；EINTR / EAGAIN 的处理 —— `src/v4l2_capture.c`
7. **并发流水线** —— 采集 / 解码 / 显示拆线程，生产者-消费者与环形缓冲 —— `docs/性能优化设计.md`
8. **解码与颜色空间** —— stb_image 与 libjpeg-turbo；RGB 上传与 YUV 直传 GPU —— `docs/性能优化设计.md`
9. **测量方法** —— 先测量再优化：FPS / CPU / 帧周期 / 端到端延迟 —— `src/main_display.c`
10. **错误与清理** —— 幂等 stop、`goto fail` 的释放顺序 —— `src/v4l2_capture.c`
11. **平台差异（答辩项）** —— UVC（uvcvideo + 元数据节点）与 SoC（media controller / subdev / ISP / CSI-2） —— `docs/V4L2结构体与能力树.md`
12. **工具链** —— v4l2-ctl / media-ctl / v4l2-compliance 各自解决什么问题

## 检验清单

- [ ] 能默写九步，并说出每一步失败的后果
- [ ] 能讲清 mmap 与 read 的区别，以及本机为什么只能用 mmap
- [ ] 能解释 S_FMT 的讲价机制，并举一个回读校验救命的例子
- [ ] 能解释 sizeimage 与 bytesused 的区别，以及 MJPG 下为什么前者不能当帧长
- [ ] 能讲清 QBUF / DQBUF 的所有权流转，以及忘记 QBUF 的两种表现
- [ ] 能解释多缓冲的作用，并说清"缓冲越多延迟越大"
- [ ] 能说出 timestamp 的时钟源、sequence 的用途，以及怎么用它们证明丢帧
- [ ] 能说出 poll + 非阻塞 DQBUF 相比阻塞式的好处
- [ ] 能正确处理 EINTR 与 EAGAIN
- [ ] 能画出多线程流水线，并说清帧数据在队列里的生命周期（什么时候才能 QBUF）
- [ ] 能给出至少 4 种降低 CPU 占用的手段，并说明各自的代价
- [ ] 能设计端到端延迟的测量方案，并解释为什么不能只看 FPS
- [ ] 能讲清 UVC 与 SoC 摄像头在 V4L2 里的结构差异
