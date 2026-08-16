/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<mayanping@email.com>
 *                  All rights reserved.
 *
 *       Filename:  main.c
 *    Description:  RV1106 端 EdgeCam 主程序
 *                  V4L2 MPLANE 采集 (NV12) → MPP 硬件编码 (H264) → RTSP 推流
 *
 *                  ===== 与 PC 端 main.c 的差异 =====
 *                  | PC 端                    | RV1106 端                |
 *                  |--------------------------|--------------------------|
 *                  | V4L2 单平面 (YUYV)        | V4L2 MPLANE (NV12)       |
 *                  | x264 软件编码             | MPP 硬件编码              |
 *                  | 编码器预热可选             | 必须预热（SPS/PPS 预热）  |
 *                  | 推流设备：/dev/video0     | 推流设备：/dev/video11    |
 *                  | 输出文件：output/         | 输出文件：/tmp/           |
 *
 *        Version:  1.0.0(2026/08/05)
 *         Author:  Mayanping <mayanping@email.com>
 *      ChangeLog:  1, Release initial version on "2026/08/05 08:54:06"
 *
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <sys/stat.h>

#include "rtsp_server.h"
#include "rtp_sender.h"
#include "v4l2_capture.h"
#include "mpp_encoder.h"

/* ===== 采集与编码参数 ===== */
#define WIDTH   640                     /* 图像宽度 */
#define HEIGHT  480                     /* 图像高度 */
#define FPS     30                      /* 帧率 */
#define BITRATE 2000                    /* 码率 (kbps) */


/* ===== 全局状态 ===== */
static volatile int g_running = 1;      /* 程序运行状态 (1=运行, 0=退出) */


/* ===== 信号处理 ===== */

/**
 * signal_handler() - 信号处理函数
 * @sig: 信号编号 (SIGINT, SIGTERM)
 *
 * 收到 Ctrl+C 或 kill 信号时，设置 g_running = 0，
 * 通知所有循环退出。
 */
static void signal_handler(int sig)
{
    printf("\n收到信号 %d, 正在退出...\n", sig);
    g_running = 0;
}


/* ===== 调试工具 ===== */

/**
 * hex_dump_main() - 十六进制打印（调试用）
 * @tag: 打印标签
 * @data: 数据指针
 * @len: 数据长度
 * @max_len: 最大打印长度
 *
 * 用于调试时查看编码输出、NV12 数据等内容的十六进制表示。
 */
static void hex_dump_main(const char *tag, const uint8_t *data, int len, int max_len)
{
    if (!data || len <= 0)
    {
        return;
    }

    int print_len = (len < max_len) ? len : max_len;
    printf("[%s] len=%d: ", tag, len);
    for (int i = 0; i < print_len; i++)
    {
        printf("%02x ", data[i]);
    }
    if (len > max_len)
    {
        printf("...");
    }
    printf("\n");
}


/* ===== RTSP PLAY 回调：推流主循环 ===== */

/**
 * camera_loop() - RTSP PLAY 回调函数（独立线程运行）
 * @arg: 未使用
 * Return: NULL
 *
 * ===== 完整推流流程 =====
 * 1. 初始化 V4L2 MPLANE 采集（/dev/video11, NV12, 640x480）
 * 2. 初始化 MPP 硬件编码器（H264, GOP=30, CBR=2Mbps）
 * 3. 循环：DQBUF 取 NV12 帧 → MPP 编码 → RTP 发送
 * 4. 同时保存 H264 文件到 /tmp/test.h264（调试用）
 * 5. 收到停止信号后退出循环，清理资源
 *
 * ===== 与 PC 端的差异 =====
 * - 使用 MPLANE 接口（非单平面）
 * - 输入格式为 NV12（非 YUYV）
 * - 使用 MPP 硬编码（非 x264 软编码）
 * - 编码输出为 vendor packet（非标准 Annex B）
 * - 设备路径为 /dev/video11（非 /dev/video0）
 */
void *camera_loop(void *arg)
{
    v4l2_capture_t cap;
    MppEncoder enc;
    int ret;
    void *nv12_data;
    size_t data_len;
    int buf_index;
    uint8_t *h264_data = NULL;
    int h264_len = 0;

    /* ===== 调试用：保存 NV12 原始帧 ===== */
    static int nv12_saved = 0;

    /* ===== 1. 打开 H264 文件（调试用） ===== */
    FILE *h264_file = fopen("/tmp/test.h264", "wb");
    if (!h264_file)
    {
        printf("[Main-ERROR] 无法创建测试文件\n");
        return NULL;
    }
    printf("[Main] 测试文件 /tmp/test.h264 已创建\n");

    printf("[Main] camera_loop 线程启动\n");

    /* ===== 2. 初始化 V4L2 采集 ===== */
    ret = v4l2_capture_init(&cap, "/dev/video11", WIDTH, HEIGHT);
    if (ret < 0)
    {
        printf("[Main-ERROR] V4L2初始化失败, ret=%d\n", ret);
        fclose(h264_file);
        return NULL;
    }
    printf("[Main-DEBUG] V4L2初始化成功: %dx%d, stride=%d, image_size=%zu\n",
           cap.width, cap.height, cap.stride, cap.image_size);

    /* ===== 3. 初始化 MPP 硬件编码器 ===== */
    ret = mpp_encoder_init_ex(&enc, cap.width, cap.height, FPS, 30, BITRATE * 1000);
    if (ret < 0)
    {
        printf("[Main-ERROR] MPP编码器初始化失败, ret=%d\n", ret);
        v4l2_capture_cleanup(&cap);
        fclose(h264_file);
        return NULL;
    }
    printf("[Main-DEBUG] MPP编码器初始化成功 (GOP=30)\n");

    /* ===== 4. 启动视频流 ===== */
    ret = v4l2_capture_start(&cap);
    if (ret < 0)
    {
        printf("[Main-ERROR] V4L2启动流式传输失败, ret=%d\n", ret);
        mpp_encoder_close(&enc);
        v4l2_capture_cleanup(&cap);
        fclose(h264_file);
        return NULL;
    }
    printf("[Main-DEBUG] V4L2流式采集已启动\n");

    printf("[Main] 开始采集编码循环\n");
    printf("[Main] V4L2 stride=%d, image_size=%zu\n", cap.stride, cap.image_size);

    int frame_count_total = 0;

    /* ===== 5. 主循环：采集 → 编码 → 推流 ===== */
    while (g_running && rtsp_is_streaming())
    {
        /* ---- 5.1 采集一帧 NV12 ---- */
        ret = v4l2_capture_frame(&cap, &nv12_data, &data_len, &buf_index);
        if (ret == -2)
        {
            usleep(10000);
            continue;
        }
        else if (ret < 0)
        {
            printf("[Main-ERROR] 获取帧失败, ret=%d\n", ret);
            break;
        }

        /* ---- 5.2 保存 NV12 原始帧（调试，仅第一帧） ---- */
        if (!nv12_saved && nv12_data && data_len > 0)
        {
            FILE *nv12_file = fopen("/tmp/capture.nv12", "wb");
            if (nv12_file)
            {
                fwrite(nv12_data, 1, data_len, nv12_file);
                fclose(nv12_file);
                printf("[Main] ★★★ 已保存 NV12 帧到 /tmp/capture.nv12, size=%zu ★★★\n", data_len);
                nv12_saved = 1;
            }
            else
            {
                printf("[Main-ERROR] 无法保存 NV12 文件\n");
            }
        }

        /* ---- 5.3 MPP 硬件编码 ---- */
        ret = mpp_encoder_encode(&enc, (const uint8_t*)nv12_data,
                                  (int)data_len, &h264_data);
        v4l2_capture_release_frame(&cap, buf_index);

        /* ---- 5.4 处理编码输出 ---- */
        if (ret > 0 && h264_data != NULL)
        {
            h264_len = ret;

            printf("[Main-DEBUG] 帧#%d: 编码成功, len=%d\n",
                   frame_count_total, h264_len);

            /* 写文件：添加 4 字节起始码后保存（Annex B 格式） */
            uint8_t startcode[4] = {0x00, 0x00, 0x00, 0x01};
            fwrite(startcode, 1, 4, h264_file);
            fwrite(h264_data, 1, h264_len, h264_file);
            fflush(h264_file);

            /* RTP 推流：直接发送裸 NALU（不含起始码） */
            rtp_send_nalu(h264_data, h264_len);
            rtp_next_frame();           /* 每帧推进一次时间戳 */

            free(h264_data);
            h264_data = NULL;
        }
        else if (ret == 0)
        {
            /* 编码器需要积累更多帧（MPP 编码器的异步特性） */
            printf("[Main-DEBUG] 帧#%d: 编码器需要更多帧\n", frame_count_total);
        }
        else
        {
            printf("[Main-ERROR] 帧#%d: 编码失败, ret=%d\n", frame_count_total, ret);
        }

        frame_count_total++;
        usleep(1000000 / FPS);          /* 控节奏 ~30fps */
    }

    printf("[Main] 循环退出, 共编码 %d 帧\n", frame_count_total);
    fclose(h264_file);
    printf("[Main] H264 文件已保存到 /tmp/test.h264\n");

    /* ===== 6. 清理资源 ===== */
    if (h264_data)
    {
        free(h264_data);
        h264_data = NULL;
    }
    mpp_encoder_close(&enc);
    v4l2_capture_cleanup(&cap);

    printf("[Main] camera_loop 线程退出\n");
    return NULL;
}


/* ===== 摄像头预检 ===== */

/**
 * preinit_camera() - 验证摄像头设备是否存在
 * Return: 0 成功，-1 失败
 *
 * 仅检查 /dev/video11 设备节点是否存在，不做实际采集。
 * 真正的采集初始化在 camera_loop 中完成。
 */
int preinit_camera(void)
{
    struct stat st;
    if (stat("/dev/video11", &st) < 0)
    {
        printf("[Main-ERROR] 摄像头 /dev/video11 不存在\n");
        return -1;
    }
    printf("[Main] 摄像头 /dev/video11 正常\n");
    
    return 0;
}


/* ===== 编码器预热（RV1106 特有） ===== */

/**
 * warmup_encoder() - 预热编码器，提前提取 SPS/PPS
 * Return: 0 成功，-1 失败
 *
 * ===== 为什么需要预热？ =====
 * PC 端 x264 编码器在 DESCRIBE 之前就能输出 SPS/PPS，
 * 但 RV1106 的 MPP 编码器需要实际编码一帧才会产生 SPS/PPS。
 *
 * 而 RTSP 的 DESCRIBE 在 PLAY 之前发送，如果没有提前提取 SPS/PPS，
 * SDP 中只能使用占位符，导致 VLC 无法解码。
 *
 * ===== 预热流程 =====
 * 1. 初始化 V4L2 采集和 MPP 编码器
 * 2. 采集一帧并编码
 * 3. 从编码输出中查找 SPS (0x67) 和 PPS (0x68)
 * 4. 调用 rtsp_set_sps_pps() 通知 RTSP 服务器
 * 5. 清理资源，等待 VLC 连接
 *
 * 这样在 VLC 发送 DESCRIBE 时，SPS/PPS 已经就绪。
 */
static int warmup_encoder(void)
{
    v4l2_capture_t cap;
    MppEncoder enc;
    int ret;
    void *nv12_data;
    size_t data_len;
    int buf_index;
    int timeout = 50;                       /* 最多尝试 50 帧 */
    int sps_pps_ready = 0;
    uint8_t *h264_data = NULL;
    int h264_len = 0;

    printf("[Main] 预热编码器以获取 SPS/PPS...\n");

    /* 1. 初始化 V4L2 采集 */
    ret = v4l2_capture_init(&cap, "/dev/video11", WIDTH, HEIGHT);
    if (ret < 0)
    {
        printf("[Main-ERROR] 预热: V4L2初始化失败, ret=%d\n", ret);
        return -1;
    }

    /* 2. 初始化 MPP 编码器 */
    ret = mpp_encoder_init_ex(&enc, cap.width, cap.height, FPS, 30, BITRATE * 1000);
    if (ret < 0)
    {
        printf("[Main-ERROR] 预热: MPP编码器初始化失败, ret=%d\n", ret);
        v4l2_capture_cleanup(&cap);
        return -1;
    }

    /* 3. 启动视频流 */
    ret = v4l2_capture_start(&cap);
    if (ret < 0)
    {
        printf("[Main-ERROR] 预热: V4L2启动流式传输失败, ret=%d\n", ret);
        mpp_encoder_close(&enc);
        v4l2_capture_cleanup(&cap);
        return -1;
    }

    printf("[Main] 预热: 开始采集编码, 等待 SPS/PPS...\n");

    /* 4. 循环编码直到提取到 SPS/PPS */
    while (timeout-- > 0 && !sps_pps_ready)
    {
        ret = v4l2_capture_frame(&cap, &nv12_data, &data_len, &buf_index);
        if (ret == -2)
        {
            usleep(50000);
            continue;
        }
        else if (ret < 0)
        {
            printf("[Main-ERROR] 预热: 获取帧失败, ret=%d\n", ret);
            break;
        }

        ret = mpp_encoder_encode(&enc, (const uint8_t*)nv12_data,
                                  (int)data_len, &h264_data);
        v4l2_capture_release_frame(&cap, buf_index);

        if (ret > 0 && h264_data != NULL)
        {
            h264_len = ret;

            /* ---- 查找 SPS (0x67) 和 PPS (0x68) ---- */
            uint8_t *sps = NULL;
            uint8_t *pps = NULL;
            int sps_len = 0, pps_len = 0;

            for (int i = 0; i < h264_len - 4; i++)
            {
                /* 查找 4 字节起始码: 00 00 00 01 */
                if (h264_data[i] == 0x00 && h264_data[i+1] == 0x00 &&
                    h264_data[i+2] == 0x00 && h264_data[i+3] == 0x01)
                {
                    uint8_t nal_type = h264_data[i+4] & 0x1F;

                    if (nal_type == 7)
                    {        /* SPS */
                        sps = h264_data + i + 4;
                        int next_start = 0;
                        for (int j = i + 5; j < h264_len - 3; j++)
                        {
                            if (h264_data[j] == 0x00 && h264_data[j+1] == 0x00 &&
                                h264_data[j+2] == 0x00 && h264_data[j+3] == 0x01)
                            {
                                next_start = j;
                                break;
                            }
                        }
                        sps_len = (next_start > 0) ? next_start - (i + 4) : h264_len - (i + 4);
                        printf("[Main] 预热: 找到 SPS, len=%d\n", sps_len);
                    }

                    if (nal_type == 8)
                    {        /* PPS */
                        pps = h264_data + i + 4;
                        int next_start = 0;
                        for (int j = i + 5; j < h264_len - 3; j++)
                        {
                            if (h264_data[j] == 0x00 && h264_data[j+1] == 0x00 &&
                                h264_data[j+2] == 0x00 && h264_data[j+3] == 0x01)
                            {
                                next_start = j;
                                break;
                            }
                        }
                        pps_len = (next_start > 0) ? next_start - (i + 4) : h264_len - (i + 4);
                        printf("[Main] 预热: 找到 PPS, len=%d\n", pps_len);
                    }
                }
            }

            /* 如果 SPS 和 PPS 都找到了，通知 RTSP 服务器 */
            if (sps && pps && sps_len > 0 && pps_len > 0)
            {
                rtsp_set_sps_pps(sps, sps_len, pps, pps_len);
                sps_pps_ready = 1;
                printf("[Main] ★★★ 预热: SPS/PPS 已就绪! ★★★\n");
            }

            free(h264_data);
            h264_data = NULL;
        }

        usleep(1000000 / FPS);
    }

    /* 5. 停止采集并清理 */
    v4l2_capture_stop(&cap);
    mpp_encoder_close(&enc);
    v4l2_capture_cleanup(&cap);

    if (!sps_pps_ready)
    {
        printf("[Main-WARN] 预热: SPS/PPS 获取超时!\n");
        return -1;
    }

    printf("[Main] 预热完成, SPS/PPS 已就绪\n");
    return 0;
}


/* ===== 主函数 ===== */

/**
 * main() - RV1106 端 EdgeCam 入口
 * @argc: 参数个数
 * @argv: 参数数组
 * Return: 0 成功，非 0 失败
 *
 * ===== 工作流程 =====
 * 1. 注册信号处理 (SIGINT, SIGTERM)
 * 2. 验证摄像头设备是否存在 (/dev/video11)
 * 3. 预热编码器，提前提取 SPS/PPS（RV1106 特有）
 * 4. 注册 RTSP PLAY 回调 (camera_loop)
 * 5. 初始化 RTSP 服务器 (端口 8554)
 * 6. 运行 RTSP 服务器（阻塞，直到 TEARDOWN）
 */
int main(int argc, char **argv)
{
    printf("========================================\n");
    printf("  EdgeCam_RV1106 - 摄像头实时推流\n");
    printf("  分辨率: %dx%d, 帧率: %d FPS, 码率: %d kbps\n",
           WIDTH, HEIGHT, FPS, BITRATE);
    printf("  编码: MPP 硬件编码 (H264)\n");
    printf("  采集: V4L2 MPLANE (NV12)\n");
    printf("========================================\n\n");

    /* ===== 1. 注册信号处理 ===== */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* ===== 2. 验证摄像头设备 ===== */
    if (preinit_camera() < 0)
    {
        printf("[Main] 摄像头初始化失败\n");
        return -1;
    }

    /* ===== 3. 预热编码器，提前提取 SPS/PPS（RV1106 特有） ===== */
    if (warmup_encoder() < 0)
    {
        printf("[Main-WARN] 编码器预热失败, SPS/PPS 未就绪\n");
        printf("[Main] 将继续启动, 但 VLC 可能无法解码\n");
    }

    /* ===== 4. 注册 RTSP 回调 ===== */
    rtsp_set_play_action(camera_loop);
    printf("[Main] RTSP回调已注册\n");

    /* ===== 5. 启动 RTSP 服务器 ===== */
    int fd = rtsp_server_init(8554);
    if (fd < 0)
    {
        printf("[Main-ERROR] RTSP服务器启动失败\n");
        return -1;
    }

    printf("[Main] RTSP服务器已启动，等待VLC连接...\n");
    printf("[Main] 推流地址: rtsp://[板子IP]:8554/live\n\n");

    /* ===== 6. 运行服务器（阻塞） ===== */
    rtsp_server_run(fd);

    g_running = 0;
    close(fd);
    printf("[Main] 程序退出\n");
    return 0;
}