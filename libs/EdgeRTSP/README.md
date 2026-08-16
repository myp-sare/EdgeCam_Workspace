# 轻量级 C 语言 RTSP 服务器
轻量级 C 语言 RTSP 服务器，支持 H264 裸流推送，已封装为静态库便于嵌入式集成

## 1. 编译项目
```bash
make        # 编译完整可执行文件 (RTSP Server Demo)
make lib    # 仅编译静态库 librtpsender.a
```

## 2. 运行
首先确保 Ubuntu 防火墙放行 8554 端口，在项目根目录执行：
```bash
make run
```
- RTSP 地址：`rtsp://<你的UbuntuIP>:8554/live`
- 播放方式：VLC 媒体播放器打开以上地址即可接收视频流

## 3. 项目结构
```
.
├── include/          # 公共头文件
│   └── rtsp_server.h
├── src/              # 核心源码
│   ├── main.c        # 主程序入口
│   ├── rtsp_server.c
│   ├── rtp_sender.c
│   └── utils.c
├── build/            # 编译产物（自动生成，Git忽略）
│   ├── rtsp_server
│   └── librtpsender.a
└── output.h264       # H264 测试视频源
```

## 4. 核心特性
-  模块化设计：RTSP 协议处理、RTP 发送逻辑封装为静态库
-  轻量高效：纯 C 实现，适配嵌入式 Linux 边缘设备
-  易于集成：引入 `librtpsender.a` 与对应头文件，快速接入现有业务代码
