# 轻量级 C 语言 摄像头 RTSP 推流应用
基于 V4L2 采集 + x264 编码 + EdgeRTSP 静态库，实现摄像头实时 RTSP 推流，已验证 VLC 可正常播放

## 1. 前置依赖
- **EdgeRTSP 静态库**（提供 RTSP Server 与 RTP Sender）
  bash
  cd ../EdgeRTSP && make lib

- **x264 编码器库**
- **摄像头设备**（默认 `/dev/video0`，YUYV 格式，640×480@30fps）

## 2. 编译项目
bash
make                          # 编译可执行文件 edgecam（默认从 ~/workspace/EdgeRTSP 找库）
make EDGERTSP=/path/to/EdgeRTSP   # 自定义 EdgeRTSP 路径


## 3. 运行
首先确保 Ubuntu 防火墙放行 8554 端口，在项目根目录执行：
bash
make run

- RTSP 地址：`rtsp://<你的UbuntuIP>:8554/live`
- 播放方式：VLC 媒体播放器打开以上地址即可接收视频流

## 4. 项目结构
```
.
├── include/          # 公共头文件
│   ├── v4l2_capture.h
│   ├── pixel_convert.h
│   └── h264_encoder.h
├── src/              # 核心源码
│   ├── main.c        # 主程序入口 + camera_loop 抓帧推流线程
│   ├── v4l2_capture.c  # V4L2 摄像头采集（七步初始化）
│   ├── pixel_convert.c # YUYV→I420 / YUYV→RGB / RGB→BMP 格式转换
│   └── h264_encoder.c  # x264 编码封装
├── build/            # 编译产物（自动生成，Git忽略）
│   └── edgecam
└── output/           # 运行输出（自动生成，Git忽略）
    └── test.h264     # 编码后的 H264 裸流（用于验证）
```

## 5. 核心特性
- 分层架构：采集 / 转换 / 编码 / 推流 四层解耦，复用 EdgeRTSP 通用推流库
- 轻量高效：纯 C 实现，适配嵌入式 Linux 边缘设备
- 格式转换内置：YUYV→I420 直供 x264，另备 YUYV→RGB / RGB→BMP 便于调试与后续视觉推理
