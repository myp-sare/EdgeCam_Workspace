/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  h264_encoder.h
 *    Description:  x264 软件编码器封装（PC 端）
 *                  YUV I420 → H264 编码，支持提取 SPS/PPS
 *                 
 *        Version:  1.0.0(2026/07/26)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/26 13:44:54"
 *                 
 ********************************************************************************/

#ifndef H264_ENCODER_H
#define H264_ENCODER_H

#include <x264.h>

/**
 * struct h264_encoder_t - x264 编码器上下文
 * @encoder: x264 编码器句柄
 * @pic_in: 输入图像帧（YUV I420）
 * @pic_out: 输出图像帧（编码后）
 * @width: 图像宽度
 * @height: 图像高度
 * @fps: 帧率
 * @sps_pps_extracted: 是否已提取 SPS/PPS（首次编码时自动提取）
 */
typedef struct {
    x264_t *encoder;                /* x264 编码器句柄 */
    x264_picture_t pic_in;          /* 输入帧（I420） */
    x264_picture_t pic_out;         /* 输出帧（编码后） */
    int width;                      /* 图像宽度 */
    int height;                     /* 图像高度 */
    int fps;                        /* 帧率 */
    int sps_pps_extracted;          /* 是否已提取 SPS/PPS（1=已提取） */
} h264_encoder_t;

/* ===== 编码器生命周期 ===== */

/**
 * h264_encoder_init() - 初始化 x264 编码器
 * @enc: 编码器上下文
 * @width: 图像宽度
 * @height: 图像高度
 * @fps: 帧率
 *
 * 使用 "ultrafast" 预设和 "zerolatency" 调优，适合低延迟实时推流。
 * Return: 0 成功，-1 失败
 */
int h264_encoder_init(h264_encoder_t *enc, int width, int height, int fps);

/**
 * h264_encoder_encode() - 编码一帧 YUV I420 图像
 * @enc: 编码器上下文
 * @i420: I420 数据指针（Y 平面 + U 平面 + V 平面连续存储）
 * @pts: 时间戳（用于码率控制）
 * @nals: 输出参数，指向 NALU 数组的指针
 * @nnal: 输出参数，NALU 数量
 *
 * 编码后返回 NALU 数组，调用者负责发送。
 * 首次编码会自动提取 SPS/PPS 并通过 rtsp_set_sps_pps() 通知 RTSP 服务器。
 * Return: 0 成功，<0 失败
 */
int h264_encoder_encode(h264_encoder_t *enc, const uint8_t *i420, int pts,
                         x264_nal_t **nals, int *nnal);

/**
 * h264_encoder_flush() - 刷出编码器缓存中的剩余帧
 * @enc: 编码器上下文
 * @nals: 输出参数，指向 NALU 数组的指针
 * @nnal: 输出参数，NALU 数量
 *
 * 当所有输入帧送入后调用，获取编码器缓存的剩余数据。
 * Return: 0 成功，<0 失败
 */
int h264_encoder_flush(h264_encoder_t *enc, x264_nal_t **nals, int *nnal);

/**
 * h264_encoder_cleanup() - 释放编码器资源
 * @enc: 编码器上下文
 */
void h264_encoder_cleanup(h264_encoder_t *enc);

#endif /* H264_ENCODER_H */