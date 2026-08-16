/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  main.c
 *    Description:  PC 端 EdgeCam 主程序
 *                  V4L2 采集 (YUYV) → 格式转换 (I420) → x264 编码 → RTSP 推流
 *                  完整实现：摄像头采集 → 软件编码 → RTP 打包 → RTSP 推流
 *                 
 *        Version:  1.0.0(2026/07/01)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/01 13:44:07"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <stdint.h>
#include <x264.h>
#include "rtsp_server.h"   /* rtsp_set_play_action / rtsp_server_init / rtsp_server_run / rtsp_is_streaming */
#include "rtp_sender.h"    /* rtp_send_nalu / rtp_next_frame */
#include "v4l2_capture.h"
#include "pixel_convert.h"
#include "h264_encoder.h"

/* ===== 采集参数 ===== */
#define WIDTH  640
#define HEIGHT 480
#define FPS    30


/* ===== 工具函数 ===== */

/**
 * strip_startcode() - 剥除 Annex B 起始码，返回裸 NALU
 * @pp: 输入/输出，指向数据起始位置的指针
 * @plen: 输入/输出，数据长度
 *
 * 支持 4 字节起始码 (00 00 00 01) 和 3 字节起始码 (00 00 01)。
 * 调用后 pp 指向 NALU 数据起始位置，plen 为 NALU 数据长度。
 * 用于将 x264 输出的 Annex B 格式转为 RTP 所需的裸 NALU。
 */
static void strip_startcode(uint8_t **pp, int *plen)
{
    uint8_t *p = *pp;
    int l = *plen;

    /* 4 字节起始码: 00 00 00 01 */
    if (l >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1)
    {
        *pp = p + 4;
        *plen = l - 4;
    }
    /* 3 字节起始码: 00 00 01 */
    else if (l >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1)
    {
        *pp = p + 3;
        *plen = l - 3;
    }
    /* 没有起始码，保持不变 */
}


/* ===== 推流主循环 ===== */

/**
 * camera_loop() - RTSP PLAY 回调函数（在独立线程中运行）
 * @arg: 未使用（保留）
 * Return: NULL
 *
 * 工作流程：
 *   1. 初始化 V4L2 采集（/dev/video0, YUYV, 640x480）
 *   2. 初始化 x264 编码器
 *   3. 循环：DQBUF 取帧 → YUYV→I420 转换 → x264 编码 → 写文件 + RTP 发送
 *   4. 收到停止信号后刷出尾帧
 *   5. 清理资源
 *
 * 推流状态由 rtsp_is_streaming() 控制，TEARDOWN 时退出循环。
 */
void *camera_loop(void *arg)
{
    v4l2_capture_t cap;
    h264_encoder_t enc;
    FILE *fp_out = NULL;

    /* ===== 1. 初始化采集 ===== */
    v4l2_capture_init(&cap, "/dev/video0", WIDTH, HEIGHT);

    /* ===== 2. 初始化编码器 ===== */
    h264_encoder_init(&enc, WIDTH, HEIGHT, FPS);

    /* ===== 3. 打开 H264 文件（调试用，同时存文件验证） ===== */
    fp_out = fopen("output/test.h264", "wb");
    if (!fp_out)
    {
        perror("fopen test.h264");
        /* 文件打开失败不阻塞推流，继续运行 */
    }

    printf("[camera_loop] 开始推流...\n");

    /* ===== 4. 主循环：持续采集 → 编码 → 推流 ===== */
    int frame_count = 0;
    while (rtsp_is_streaming())
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        /* ---- 4.1 从队列取出已填充的缓冲区 ---- */
        if (ioctl(cap.fd, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN)
            {
                usleep(1000);
                continue;
            }
            perror("VIDIOC_DQBUF");
            break;
        }

        printf("帧 #%d: 缓冲区 %d, 大小 %u 字节\n", frame_count, buf.index, buf.bytesused);

        /* ---- 4.2 格式转换: YUYV → I420 ---- */
        uint8_t *yuyv = (uint8_t *)cap.buffers[buf.index];
        uint8_t *i420 = malloc(WIDTH * HEIGHT * 3 / 2);
        yuyv_to_i420(yuyv, i420, WIDTH, HEIGHT);

        /* ---- 4.3 x264 编码 ---- */
        x264_nal_t *nals;
        int nnal;
        h264_encoder_encode(&enc, i420, frame_count, &nals, &nnal);

        /* ---- 4.4 处理编码输出 ---- */
        for (int i = 0; i < nnal; i++)
        {
            /* 写文件：保留 startcode（Annex B 格式） */
            if (fp_out)
            {
                fwrite(nals[i].p_payload, 1, nals[i].i_payload, fp_out);
            }

            /* 推流：剥除 startcode 后发送裸 NALU */
            uint8_t *p = nals[i].p_payload;
            int l = nals[i].i_payload;
            strip_startcode(&p, &l);
            if (l > 0)
            {
                rtp_send_nalu(p, l);
            }
        }

        /* ---- 4.5 时间戳推进（每帧一次） ---- */
        rtp_next_frame();

        free(i420);

        /* ---- 4.6 缓冲区重新入队 ---- */
        if (ioctl(cap.fd, VIDIOC_QBUF, &buf) < 0)
        {
            perror("VIDIOC_QBUF");
            break;
        }

        frame_count++;
        usleep(1000000 / FPS);   /* 控节奏 ~30fps */
    }

    printf("[camera_loop] 主循环退出，共编码 %d 帧\n", frame_count);

    /* ===== 5. 刷出编码器缓存中的剩余帧 ===== */
    printf("[camera_loop] 刷出尾帧...\n");
    x264_nal_t *nals;
    int nnal;
    while (h264_encoder_flush(&enc, &nals, &nnal) > 0)
    {
        for (int i = 0; i < nnal; i++)
        {
            if (fp_out)
            {
                fwrite(nals[i].p_payload, 1, nals[i].i_payload, fp_out);
            }
            uint8_t *p = nals[i].p_payload;
            int l = nals[i].i_payload;
            strip_startcode(&p, &l);
            if (l > 0)
            {
                rtp_send_nalu(p, l);
            }
        }
        rtp_next_frame();
    }

    /* ===== 6. 清理资源 ===== */
    if (fp_out)
    {
        fclose(fp_out);
        printf("[camera_loop] H264 文件已保存: output/test.h264\n");
    }
    h264_encoder_cleanup(&enc);
    v4l2_capture_cleanup(&cap);

    printf("[camera_loop] 线程退出\n");
    return NULL;
}


/* ===== 预初始化 ===== */

/**
 * preinit_encoder() - 验证摄像头设备是否存在
 * Return: 0 成功，-1 失败
 *
 * 仅打开设备验证，不做实际采集和编码。
 * SPS/PPS 在首次编码时由 h264_encoder_encode() 自动提取。
 */
int preinit_encoder(void)
{
    int fd = open("/dev/video0", O_RDWR);
    if (fd < 0)
    {
        printf("[preinit] 摄像头打开失败: /dev/video0\n");
        return -1;
    }
    close(fd);
    printf("[preinit] 摄像头正常，SPS/PPS 将在首次编码时动态提取\n");
    return 0;
}


/* ===== 主函数 ===== */

/**
 * main() - PC 端 EdgeCam 入口
 * @argc: 参数个数
 * @argv: 参数数组
 * Return: 0 成功，非 0 失败
 *
 * 工作流程：
 *   1. 验证摄像头设备是否存在
 *   2. 注册 RTSP PLAY 回调 (camera_loop)
 *   3. 初始化 RTSP 服务器 (端口 8554)
 *   4. 运行 RTSP 服务器（阻塞，直到 TEARDOWN）
 */
int main(int argc, char **argv)
{
    printf("========================================\n");
    printf("  EdgeCam_PC - 摄像头实时推流\n");
    printf("  分辨率: %dx%d, 帧率: %d FPS\n", WIDTH, HEIGHT, FPS);
    printf("  编码: x264 软件编码 (ultrafast + zerolatency)\n");
    printf("========================================\n\n");

    /* ===== 1. 验证摄像头 ===== */
    if (preinit_encoder() < 0)
    {
        printf("[main] 摄像头初始化失败，请检查硬件连接\n");
        return -1;
    }

    /* ===== 2. 注册 RTSP 回调 ===== */
    rtsp_set_play_action(camera_loop);
    printf("[main] RTSP 回调已注册\n");

    /* ===== 3. 启动 RTSP 服务器 ===== */
    int fd = rtsp_server_init(8554);
    if (fd < 0)
    {
        printf("[main] RTSP 服务器启动失败\n");
        return -1;
    }

    printf("[main] RTSP 服务器已启动，等待 VLC 连接...\n");
    printf("[main] 推流地址: rtsp://[本机IP]:8554/live\n\n");

    /* ===== 4. 运行服务器（阻塞） ===== */
    rtsp_server_run(fd);

    printf("[main] 程序退出\n");
    return 0;
}