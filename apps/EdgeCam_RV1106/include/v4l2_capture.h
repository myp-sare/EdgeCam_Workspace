/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  v4l2_capture.h
 *    Description:  V4L2 摄像头采集（RV1106 端 MPLANE 版本）
 *                  使用 MPLANE 接口，输出 NV12 格式
 *                  与 PC 端单平面版本形成对比：
 *                  - PC 端：单平面 (V4L2_BUF_TYPE_VIDEO_CAPTURE)，输出 YUYV
 *                  - RV1106：MPLANE (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)，输出 NV12
 *                 
 *        Version:  1.0.0(2026/08/05)
 *         Author:  Mayanping <mayanping@3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/08/05 08:46:02"
 *                 
 ********************************************************************************/

#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include <linux/videodev2.h>

#define MAX_BUFFERS 8

/**
 * struct v4l2_capture_t - V4L2 采集上下文（RV1106 MPLANE 版本）
 * @fd: 设备文件描述符
 * @dev_name: 设备路径（如 /dev/video11）
 * @width: 图像宽度
 * @height: 图像高度
 * @stride: 行步长（硬件 64 字节对齐后的值，可能大于 width）
 * @image_size: 单帧图像实际大小
 * @pixel_format: 像素格式（固定为 V4L2_PIX_FMT_NV12）
 * @buffer_count: 缓冲区数量
 * @buffers: MMAP 映射的缓冲区指针数组
 * @buffer_sizes: 每个缓冲区的大小
 * @buf_info: V4L2 缓冲区信息（用于 QBUF/DQBUF）
 * @is_streaming: 流状态标志（1=正在采集）
 *
 * 注意：与 PC 端不同，RV1106 使用 MPLANE 接口，缓冲区操作需要 planes 数组。
 * 输出格式为 NV12（Y 平面 + UV 交错平面），而非 PC 端的 YUYV。
 */
typedef struct {
    int fd;                             /* 设备文件描述符 */
    char dev_name[64];                  /* 设备路径 */
    int width;                          /* 图像宽度 */
    int height;                         /* 图像高度 */
    int stride;                         /* 行步长（硬件对齐后） */
    size_t image_size;                  /* 单帧图像大小 */
    uint32_t pixel_format;              /* 像素格式 (NV12) */
    int buffer_count;                   /* 缓冲区数量 */
    void *buffers[MAX_BUFFERS];         /* MMAP 映射指针 */
    size_t buffer_sizes[MAX_BUFFERS];   /* 缓冲区大小 */
    struct v4l2_buffer buf_info[MAX_BUFFERS]; /* V4L2 缓冲区信息 */
    int is_streaming;                   /* 流状态 (1=运行中) */
} v4l2_capture_t;

/* ===== 采集 API ===== */

/**
 * v4l2_capture_init() - 初始化 V4L2 采集
 * @cap: 采集上下文
 * @dev_path: 设备路径（如 /dev/video11）
 * @width: 目标宽度
 * @height: 目标高度
 *
 * 完成 V4L2 七步法（MPLANE 版本）：
 * open → QUERYCAP → S_FMT → REQBUFS → MMAP → QBUF（内部完成）
 * 注意：STREAMON 由 v4l2_capture_start() 单独调用。
 * Return: 0 成功，-1 失败
 */
int v4l2_capture_init(v4l2_capture_t *cap, const char *dev_path, int width, int height);

/**
 * v4l2_capture_start() - 启动视频流
 * @cap: 采集上下文
 *
 * VIDIOC_STREAMON，摄像头开始往缓冲区写数据。
 * Return: 0 成功，-1 失败
 */
int v4l2_capture_start(v4l2_capture_t *cap);

/**
 * v4l2_capture_frame() - 获取一帧采集数据
 * @cap: 采集上下文
 * @data: 输出参数，指向帧数据的指针
 * @len: 输出参数，帧长度（字节）
 * @index: 输出参数，缓冲区索引（用于释放）
 *
 * VIDIOC_DQBUF 从队列取出已填充的缓冲区。
 * Return: 0 成功，-2 无数据（EAGAIN），-1 失败
 */
int v4l2_capture_frame(v4l2_capture_t *cap, void **data, size_t *len, int *index);

/**
 * v4l2_capture_release_frame() - 释放已使用的缓冲区
 * @cap: 采集上下文
 * @index: 缓冲区索引（由 v4l2_capture_frame 返回）
 *
 * VIDIOC_QBUF 将缓冲区重新入队，供硬件复用。
 * Return: 0 成功，-1 失败
 */
int v4l2_capture_release_frame(v4l2_capture_t *cap, int index);

/**
 * v4l2_capture_stop() - 停止视频流
 * @cap: 采集上下文
 *
 * VIDIOC_STREAMOFF，摄像头停止写入数据。
 * Return: 0 成功，-1 失败
 */
int v4l2_capture_stop(v4l2_capture_t *cap);

/**
 * v4l2_capture_cleanup() - 释放采集资源
 * @cap: 采集上下文
 *
 * 清理顺序：STREAMOFF → munmap → close
 */
void v4l2_capture_cleanup(v4l2_capture_t *cap);

#endif /* V4L2_CAPTURE_H */