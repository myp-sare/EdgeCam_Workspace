/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<mayanping@email.com>
 *                  All rights reserved.
 *
 *       Filename:  v4l2_capture.c
 *    Description:  V4L2 采集实现（RV1106 端 MPLANE 版本）
 *                  使用 MPLANE 接口，输出 NV12 格式
 *                  完整实现 V4L2 七步法（MPLANE 版本）
 *
 *                  ===== 与 PC 端的差异 =====
 *                  | PC 端                | RV1106 端              |
 *                  |----------------------|------------------------|
 *                  | 单平面接口           | MPLANE 接口             |
 *                  | V4L2_BUF_TYPE_CAPTURE | V4L2_BUF_TYPE_CAPTURE_MPLANE |
 *                  | fmt.fmt.pix          | fmt.fmt.pix_mp         |
 *                  | 输出 YUYV            | 输出 NV12              |
 *                  | stride = width       | stride 可能 > width    |
 *                 
 *        Version:  1.0.0(2026/08/05)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/08/05 08:47:00"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include "v4l2_capture.h"


/* ===== V4L2 七步法（MPLANE 版本） ===== */

/**
 * set_v4l2_format() - V4L2 七步法第 3 步：设置图像格式
 * @cap: 采集上下文
 * Return: 0 成功，-1 失败
 *
 * 使用 MPLANE 接口设置 NV12 格式。
 * 驱动会协商实际的宽高和 stride，需要重新读取（VIDIOC_G_FMT）并保存。
 */
static int set_v4l2_format(v4l2_capture_t *cap)
{
    struct v4l2_format fmt;
    int ret;

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;          /* MPLANE 捕获 */
    fmt.fmt.pix_mp.width = cap->width;
    fmt.fmt.pix_mp.height = cap->height;
    fmt.fmt.pix_mp.pixelformat = cap->pixel_format;         /* NV12 */
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.plane_fmt[0].bytesperline = cap->width;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = cap->width * cap->height * 3 / 2;

    ret = ioctl(cap->fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0)
    {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    /* 获取实际设置的格式（驱动可能修改了参数） */
    ret = ioctl(cap->fd, VIDIOC_G_FMT, &fmt);
    if (ret < 0)
    {
        perror("VIDIOC_G_FMT");
        return -1;
    }

    /* 保存实际参数 */
    cap->width = fmt.fmt.pix_mp.width;
    cap->height = fmt.fmt.pix_mp.height;
    cap->stride = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    cap->image_size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

    printf("[V4L2] 设置格式: %dx%d, pixelformat=0x%08x\n",
           cap->width, cap->height, fmt.fmt.pix_mp.pixelformat);
    printf("[V4L2] stride=%d, image_size=%zu\n", cap->stride, cap->image_size);

    return 0;
}

/**
 * request_and_mmap_buffers() - V4L2 七步法第 4-5 步：申请并映射缓冲区
 * @cap: 采集上下文
 * Return: 0 成功，-1 失败
 *
 * 1. VIDIOC_REQBUFS 申请内核缓冲区
 * 2. 逐个 VIDIOC_QUERYBUF 查询缓冲区信息
 * 3. mmap() 映射到用户空间（零拷贝）
 *
 * MPLANE 版本需要 planes 数组，与 PC 端单平面不同。
 */
static int request_and_mmap_buffers(v4l2_capture_t *cap)
{
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];
    int ret;

    /* ---- 第 4 步：申请缓冲区 ---- */
    memset(&req, 0, sizeof(req));
    req.count = MAX_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(cap->fd, VIDIOC_REQBUFS, &req);
    if (ret < 0)
    {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    if (req.count < 2)
    {
        printf("[V4L2] 缓冲区不足: %d\n", req.count);
        return -1;
    }

    cap->buffer_count = req.count;
    printf("[V4L2] 分配了 %d 个缓冲区\n", cap->buffer_count);

    /* ---- 第 5 步：查询并映射缓冲区 ---- */
    for (int i = 0; i < cap->buffer_count; i++)
    {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;          /* MPLANE 需要 planes 数组 */
        buf.length = 1;

        ret = ioctl(cap->fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0)
        {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        cap->buffers[i] = mmap(NULL, buf.m.planes[0].length,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED,
                               cap->fd, buf.m.planes[0].m.mem_offset);
        if (cap->buffers[i] == MAP_FAILED)
        {
            perror("mmap");
            return -1;
        }

        cap->buffer_sizes[i] = buf.m.planes[0].length;
        memcpy(&cap->buf_info[i], &buf, sizeof(buf));

        printf("[V4L2] 缓冲区 %d: 映射到 %p, 大小 %zu\n",
               i, cap->buffers[i], buf.m.planes[0].length);
    }

    return 0;
}

/**
 * queue_all_buffers() - V4L2 七步法第 6 步：缓冲区入队
 * @cap: 采集上下文
 * Return: 0 成功，-1 失败
 *
 * VIDIOC_QBUF 把所有缓冲区放入内核队列，
 * 摄像头硬件会从队列中取空缓冲区填充数据。
 * MPLANE 版本同样需要 planes 数组。
 */
static int queue_all_buffers(v4l2_capture_t *cap)
{
    for (int i = 0; i < cap->buffer_count; i++)
    {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1;

        int ret = ioctl(cap->fd, VIDIOC_QBUF, &buf);
        if (ret < 0)
        {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }
    return 0;
}


/* ===== 公共 API ===== */

/**
 * v4l2_capture_init() - V4L2 七步法第 1-6 步
 * @cap: 采集上下文
 * @dev_path: 设备路径
 * @width: 目标宽度
 * @height: 目标高度
 * Return: 0 成功，-1 失败
 *
 * 完成步骤：open → QUERYCAP → S_FMT → REQBUFS → MMAP → QBUF
 * STREAMON 由 v4l2_capture_start() 单独调用。
 */
int v4l2_capture_init(v4l2_capture_t *cap, const char *dev_path, int width, int height)
{
    if (!cap)
    {
        return -1;
    }

    memset(cap, 0, sizeof(v4l2_capture_t));

    strncpy(cap->dev_name, dev_path, sizeof(cap->dev_name) - 1);
    cap->width = width;
    cap->height = height;
    cap->pixel_format = V4L2_PIX_FMT_NV12;
    cap->is_streaming = 0;
    cap->stride = width;
    cap->image_size = width * height * 3 / 2;

    /* ---- 第 1 步：打开设备 ---- */
    cap->fd = open(dev_path, O_RDWR | O_NONBLOCK);
    if (cap->fd < 0)
    {
        perror("open /dev/video11");
        return -1;
    }
    printf("[V4L2] 打开设备 %s 成功\n", dev_path);

    /* ---- 第 2 步：查询设备能力 ---- */
    struct v4l2_capability vcap;
    if (ioctl(cap->fd, VIDIOC_QUERYCAP, &vcap) < 0)
    {
        perror("VIDIOC_QUERYCAP");
        close(cap->fd);
        return -1;
    }
    /* 能力检查在外部或此处可添加 */

    /* ---- 第 3 步：设置图像格式 ---- */
    if (set_v4l2_format(cap) < 0)
    {
        close(cap->fd);
        return -1;
    }

    /* ---- 第 4-5 步：申请并映射缓冲区 ---- */
    if (request_and_mmap_buffers(cap) < 0)
    {
        close(cap->fd);
        return -1;
    }

    /* ---- 第 6 步：缓冲区入队 ---- */
    if (queue_all_buffers(cap) < 0)
    {
        close(cap->fd);
        return -1;
    }

    printf("[V4L2] 初始化完成: %s, %dx%d, NV12, stride=%d, image_size=%zu\n",
           dev_path, cap->width, cap->height, cap->stride, cap->image_size);

    return 0;
}

/**
 * v4l2_capture_start() - V4L2 七步法第 7 步：启动视频流
 * @cap: 采集上下文
 * Return: 0 成功，-1 失败
 *
 * VIDIOC_STREAMON，摄像头开始往缓冲区写数据。
 */
int v4l2_capture_start(v4l2_capture_t *cap)
{
    if (!cap || cap->is_streaming)
    {
        return -1;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    int ret = ioctl(cap->fd, VIDIOC_STREAMON, &type);
    if (ret < 0)
    {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    cap->is_streaming = 1;
    printf("[V4L2] 流式采集已启动\n");
    return 0;
}

/**
 * v4l2_capture_frame() - 从队列取出已填充的缓冲区
 * @cap: 采集上下文
 * @data: 输出，帧数据指针
 * @len: 输出，帧长度
 * @index: 输出，缓冲区索引
 * Return: 0 成功，-2 无数据（EAGAIN），-1 失败
 *
 * VIDIOC_DQBUF 取帧，MPLANE 版本需要 planes 数组。
 */
int v4l2_capture_frame(v4l2_capture_t *cap, void **data, size_t *len, int *index)
{
    if (!cap || !cap->is_streaming)
    {
        return -1;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];
    int ret;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1;

    ret = ioctl(cap->fd, VIDIOC_DQBUF, &buf);
    if (ret < 0)
    {
        if (errno == EAGAIN)
        {
            return -2;          /* 无数据，稍后重试 */
        }
        perror("VIDIOC_DQBUF");
        return -1;
    }

    *data = cap->buffers[buf.index];
    *len = buf.m.planes[0].bytesused;
    *index = buf.index;

    return 0;
}

/**
 * v4l2_capture_release_frame() - 将缓冲区重新入队
 * @cap: 采集上下文
 * @index: 缓冲区索引
 * Return: 0 成功，-1 失败
 *
 * VIDIOC_QBUF 将缓冲区放回队列，供硬件复用。
 */
int v4l2_capture_release_frame(v4l2_capture_t *cap, int index)
{
    if (!cap || index < 0 || index >= cap->buffer_count)
    {
        return -1;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.m.planes = planes;
    buf.length = 1;

    int ret = ioctl(cap->fd, VIDIOC_QBUF, &buf);
    if (ret < 0)
    {
        perror("VIDIOC_QBUF");
        return -1;
    }

    return 0;
}

/**
 * v4l2_capture_stop() - 停止视频流
 * @cap: 采集上下文
 * Return: 0 成功，-1 失败
 *
 * VIDIOC_STREAMOFF，摄像头停止写入数据。
 */
int v4l2_capture_stop(v4l2_capture_t *cap)
{
    if (!cap || !cap->is_streaming)
    {
        return -1;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    int ret = ioctl(cap->fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0)
    {
        perror("VIDIOC_STREAMOFF");
        return -1;
    }

    cap->is_streaming = 0;
    printf("[V4L2] 流式采集已停止\n");
    return 0;
}

/**
 * v4l2_capture_cleanup() - 释放所有采集资源
 * @cap: 采集上下文
 *
 * 清理顺序（不可颠倒）：
 * 1. STREAMOFF 停止流
 * 2. munmap 解除 MMAP 映射
 * 3. close 关闭设备
 */
void v4l2_capture_cleanup(v4l2_capture_t *cap)
{
    if (!cap)
    {
        return;
    }

    if (cap->is_streaming)
    {
        v4l2_capture_stop(cap);
    }

    for (int i = 0; i < cap->buffer_count; i++)
    {
        if (cap->buffers[i] && cap->buffers[i] != MAP_FAILED)
        {
            munmap(cap->buffers[i], cap->buffer_sizes[i]);
            cap->buffers[i] = NULL;
        }
    }

    if (cap->fd >= 0)
    {
        close(cap->fd);
        cap->fd = -1;
    }

    printf("[V4L2] 资源清理完成\n");
}