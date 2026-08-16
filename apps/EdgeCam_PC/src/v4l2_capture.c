/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  v4l2_capture.c
 *    Description:  V4L2 摄像头采集实现（PC 端 USB 摄像头）
 *                  使用单平面接口，输出 YUYV 格式
 *                 完整实现 V4L2 七步法
 *                 
 *        Version:  1.0.0(2026/07/01)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/01 13:44:29"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <stdint.h>
#include "v4l2_capture.h"

/* ===== V4L2 七步法 ===== */

/**
 * v4l2_capture_init() - 初始化 V4L2 采集
 * @cap: 采集上下文
 * @dev_name: 设备路径（如 /dev/video0）
 * @width: 目标宽度
 * @height: 目标高度
 * Return: 0 成功，-1 失败
 *
 * 流程：open → QUERYCAP → S_FMT → REQBUFS → MMAP → QBUF → STREAMON
 */
int v4l2_capture_init(v4l2_capture_t *cap, const char *dev_name, int width, int height)
{
    /* ===== 第 1 步：打开设备 ===== */
    cap->fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if (cap->fd < 0)
    {
        perror("open");
        exit(1);
    }
    printf("打开设备 %s 成功, fd = %d\n", dev_name, cap->fd);

    /* ===== 第 2 步：查询设备能力 ===== */
    struct v4l2_capability vcap;
    if (ioctl(cap->fd, VIDIOC_QUERYCAP, &vcap) < 0)
    {
        perror("VIDIOC_QUERYCAP");
        close(cap->fd);
        exit(1);
    }
    printf("VIDIOC_QUERYCAP 成功\n");

    /* ===== 第 3 步：设置图像格式 ===== */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;          /* 单平面捕获 */
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;      /* YUYV 4:2:2 */
    fmt.fmt.pix.field = V4L2_FIELD_NONE;              /* 逐行扫描 */

    if (ioctl(cap->fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("VIDIOC_S_FMT");
        close(cap->fd);
        exit(1);
    }
    printf("VIDIOC_S_FMT 成功\n");

    /* ===== 第 4 步：申请内核缓冲区 ===== */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = MAX_BUFFERS;                          /* 4 个缓冲区 */
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;                    /* MMAP 零拷贝模式 */

    if (ioctl(cap->fd, VIDIOC_REQBUFS, &req) < 0)
    {
        perror("VIDIOC_REQBUFS");
        close(cap->fd);
        exit(1);
    }
    printf("内核实际分配缓冲区数目：%u\n", req.count);

    /* ===== 第 5 步：查询并映射缓冲区 ===== */
    for (int i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(cap->fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            perror("VIDIOC_QUERYBUF");
            close(cap->fd);
            exit(1);
        }

        /* 映射内核缓冲区到用户空间（零拷贝） */
        cap->buffers[i] = mmap(NULL, buf.length,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED,
                               cap->fd, buf.m.offset);
        if (cap->buffers[i] == MAP_FAILED)
        {
            perror("mmap");
            close(cap->fd);
            exit(1);
        }

        cap->buf_length[i] = buf.length;
        printf("缓冲区 %d 映射成功，大小 %u\n", i, cap->buf_length[i]);
    }
    cap->buf_count = req.count;

    /* ===== 第 6 步：缓冲区入队 ===== */
    for (int i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(cap->fd, VIDIOC_QBUF, &buf) < 0)
        {
            perror("VIDIOC_QBUF");
            close(cap->fd);
            exit(1);
        }
    }
    printf("所有缓冲区已入队\n");

    /* ===== 第 7 步：启动视频流 ===== */
    cap->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cap->fd, VIDIOC_STREAMON, &cap->type) < 0)
    {
        perror("VIDIOC_STREAMON");
        close(cap->fd);
        exit(1);
    }
    printf("VIDIOC_STREAMON 成功\n");

    return 0;
}

/**
 * v4l2_capture_cleanup() - 释放采集资源
 * @cap: 采集上下文
 *
 * 清理顺序：STREAMOFF → munmap → close
 * 顺序不可颠倒，否则可能造成内存泄漏或设备异常。
 */
void v4l2_capture_cleanup(v4l2_capture_t *cap)
{
    /* 1. 停止视频流 */
    ioctl(cap->fd, VIDIOC_STREAMOFF, &cap->type);

    /* 2. 解除 MMAP 映射 */
    for (int i = 0; i < cap->buf_count; i++)
    {
        munmap(cap->buffers[i], cap->buf_length[i]);
    }

    /* 3. 关闭设备 */
    close(cap->fd);

    printf("[V4L2] 资源清理完成\n");
}