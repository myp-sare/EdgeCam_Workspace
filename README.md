# EdgeCam Workspace

RV1106 和 PC 端的摄像头实时采集 + 硬件/软件编码 + RTSP 推流。

## 项目简介

本项目包含三个子模块，构成完整的摄像头采集 → 编码 → RTSP 推流链路：

- **libs/EdgeRTSP/**：RTSP/RTP 共享库（PC 和 RV1106 共用）
- **apps/EdgeCam_PC/**：PC 端实现（USB 摄像头 + x264 软件编码）
- **apps/EdgeCam_RV1106/**：RV1106 端实现（MIPI 摄像头 + MPP 硬件编码）

### 两端的差异

| 对比项 | PC 端 | RV1106 端 |
|--------|-------|-----------|
| 摄像头接口 | USB (/dev/video0) | MIPI CSI (/dev/video11) |
| V4L2 接口 | 单平面 | MPLANE |
| 采集格式 | YUYV | NV12 |
| 编码方式 | x264 软件编码 | MPP 硬件编码 |
| 编码延迟 | 50-100ms | <30ms |
| CPU 占用 | 80%+ | <10% |

## 目录结构

```
EdgeCam_Workspace/
├── libs/
│   └── EdgeRTSP/          # RTSP/RTP 共享库
│       ├── include/       # 头文件
│       ├── src/           # 源码
│       └── Makefile
├── apps/
│   ├── EdgeCam_PC/        # PC 端应用
│   │   ├── include/
│   │   ├── src/
│   │   └── Makefile
│   └── EdgeCam_RV1106/    # RV1106 端应用
│       ├── include/
│       ├── src/
│       └── Makefile
├── README.md
└── .gitignore
```

## 编译顺序

**必须按以下顺序编译：**

```bash
# 1. 先编译共享库（按需选择平台）
cd libs/EdgeRTSP
make clean

# PC 版本
make PLATFORM=PC lib

# RV1106 版本（需配置 SDK_PATH）
make PLATFORM=RV1106 SDK_PATH=/path/to/luckfox-pico lib

# 2. 编译应用
cd apps/EdgeCam_PC && make          # PC 端
cd apps/EdgeCam_RV1106 && make      # RV1106 端
```

## 环境准备

### PC 端

```bash
# 依赖
sudo apt install build-essential libx264-dev ffmpeg vlc
```

### RV1106 端

1. **LuckFox SDK**：下载到 `~/luckfox-dev/luckfox-pico`
2. **交叉编译工具链**：`arm-rockchip830-linux-uclibcgnueabihf`
3. **串口线**：连接板子串口，波特率 115200

```bash
# 克隆 SDK
git clone https://gitee.com/LuckfoxTECH/luckfox-pico.git ~/luckfox-dev/luckfox-pico

# 设置环境变量
export SDK_PATH=~/luckfox-dev/luckfox-pico
```

## 运行

### RV1106 端

```bash
# 1. 复制程序到板子
scp build/edgecam_rv1106 root@172.32.0.93:/root/

# 2. SSH 登录
ssh root@172.32.0.93

# 3. 启动 ISP 服务（关键！否则画面发绿）
rkaiq_3A_server &

# 4. 运行程序
./edgecam_rv1106
```

### PC 端

```bash
./build/edgecam
```

### 拉流测试

```bash
# ffplay（推荐）
ffplay rtsp://172.32.0.93:8554/live

# TCP 模式（更稳定）
ffplay -rtsp_transport tcp rtsp://172.32.0.93:8554/live

# VLC
vlc rtsp://172.32.0.93:8554/live
```
