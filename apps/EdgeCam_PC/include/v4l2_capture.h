/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  v4l2_capture.h
 *    Description:  V4L2 摄像头采集（PC 端 USB 摄像头）
 *                  使用单平面接口，输出 YUYV 格式
 *                 
 *        Version:  1.0.0(2026/07/01)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/01 13:44:50"
 *                 
 ********************************************************************************/

#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

/* 最大缓冲区数量（与内核申请数量保持一致） */
#define MAX_BUFFERS 4

/**
 * struct v4l2_capture_t - V4L2 采集上下文（PC 端 USB 摄像头）
 * @fd: 设备文件描述符
 * @buffers: MMAP 映射的缓冲区指针数组
 * @buf_length: 每个缓冲区的长度（字节）
 * @buf_count: 缓冲区数量
 * @type: 缓冲区类型（V4L2_BUF_TYPE_VIDEO_CAPTURE）
 *
 * 注意：与 RV1106 端不同，PC 端使用单平面接口（V4L2_BUF_TYPE_VIDEO_CAPTURE），
 * 输出格式为 YUYV，而 RV1106 端使用 MPLANE 接口输出 NV12。
 */
typedef struct {
    int fd;                         /* 设备文件描述符 */
    void *buffers[MAX_BUFFERS];     /* MMAP 映射的缓冲区指针 */
    unsigned int buf_length[MAX_BUFFERS]; /* 缓冲区大小 */
    int buf_count;                  /* 缓冲区数量 */
    int type;                       /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
} v4l2_capture_t;

/* ===== 采集 API ===== */

/**
 * v4l2_capture_init() - 初始化 V4L2 采集
 * @cap: 采集上下文
 * @dev_name: 设备路径（如 /dev/video0）
 * @width: 目标宽度
 * @height: 目标高度
 *
 * 完成 V4L2 七步法：
 * open → QUERYCAP → S_FMT → REQBUFS → MMAP → QBUF → STREAMON
 * 输出格式固定为 YUYV（V4L2_PIX_FMT_YUYV）。
 * Return: 0 成功，-1 失败
 */
int v4l2_capture_init(v4l2_capture_t *cap, const char *dev_name, int width, int height);

/**
 * v4l2_capture_cleanup() - 释放采集资源
 * @cap: 采集上下文
 *
 * STREAMOFF → munmap 解除映射 → close 设备
 */
void v4l2_capture_cleanup(v4l2_capture_t *cap);

#endif /* V4L2_CAPTURE_H */