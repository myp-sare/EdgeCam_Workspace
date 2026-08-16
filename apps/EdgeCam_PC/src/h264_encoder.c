/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  h264_encoder.c
 *    Description:  x264 软件编码器封装（PC 端）
 *                  YUV I420 → H264 编码，支持提取 SPS/PPS
 *                 
 *        Version:  1.0.0(2026/07/26)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/26 13:44:19"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <x264.h>
#include "h264_encoder.h"
#include "rtsp_server.h"   /* rtsp_set_sps_pps() 用于 SPS/PPS 通知 */


/* ===== 静态工具函数 ===== */

/**
 * get_nalu_type() - 获取 NALU 类型
 * @data: NALU 数据（不含起始码）
 * Return: NALU 类型（7=SPS, 8=PPS, 5=IDR, 1=P帧，详见 H264 标准）
 */
static int get_nalu_type(const uint8_t *data)
{
    if (!data){
        return -1;
    }

    return data[0] & 0x1f;
}

/**
 * skip_startcode() - 跳过起始码，返回 NALU 数据起始位置
 * @data: 可能包含起始码的数据
 * @len: 数据长度
 * @nal_len: 输出参数，NALU 数据长度（不含起始码）
 *
 * 支持 4 字节起始码（00 00 00 01）和 3 字节起始码（00 00 01）。
 * Return: NALU 数据起始指针
 */
static uint8_t* skip_startcode(uint8_t *data, int len, int *nal_len)
{
    uint8_t *p = data;
    int remaining = len;

    /* 4 字节 startcode: 00 00 00 01 */
    if (remaining >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1)
    {
        *nal_len = remaining - 4;
        return p + 4;
    }

    /* 3 字节 startcode: 00 00 01 */
    if (remaining >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1)
    {
        *nal_len = remaining - 3;
        return p + 3;
    }

    /* 没有找到起始码，整个数据视为 NALU */
    *nal_len = remaining;
    return p;
}


/* ===== 公共 API ===== */

/**
 * h264_encoder_init() - 初始化 x264 编码器
 * @enc: 编码器上下文
 * @width: 图像宽度
 * @height: 图像高度
 * @fps: 帧率
 * Return: 0 成功，-1 失败
 *
 * 配置 "ultrafast" + "zerolatency" 适合低延迟推流，
 * CRF=23 提供较好的画质/码率平衡。
 */
int h264_encoder_init(h264_encoder_t *enc, int width, int height, int fps)
{
    x264_param_t param;

    /* 配置 x264 参数 */
    x264_param_default_preset(&param, "ultrafast", "zerolatency");
    param.i_width = width;
    param.i_height = height;
    param.i_fps_num = fps;
    param.i_fps_den = 1;
    param.i_csp = X264_CSP_I420;
    param.b_repeat_headers = 1;        /* 每个 IDR 帧前重发 SPS/PPS */
    param.rc.i_rc_method = X264_RC_CRF;
    param.rc.f_rf_constant = 23;

    enc->encoder = x264_encoder_open(&param);
    x264_picture_alloc(&enc->pic_in, X264_CSP_I420, width, height);

    enc->width = width;
    enc->height = height;
    enc->fps = fps;
    enc->sps_pps_extracted = 0;        /* 初始未提取 SPS/PPS */

    return 0;
}

/**
 * h264_encoder_encode() - 编码一帧 YUV I420 图像
 * @enc: 编码器上下文
 * @i420: I420 数据
 * @pts: 时间戳
 * @nals: 输出，NALU 数组
 * @nnal: 输出，NALU 数量
 * Return: 0 成功，<0 失败
 *
 * 第一帧强制为 IDR 帧，确保解码器能正确初始化。
 * 编码后自动提取 SPS/PPS 并通知 RTSP 服务器。
 */
int h264_encoder_encode(h264_encoder_t *enc, const uint8_t *i420, int pts,
                        x264_nal_t **nals, int *nnal)
{
    /* 第一帧强制为 IDR（关键帧） */
    if (pts == 0 && !enc->sps_pps_extracted)
    {
        enc->pic_in.i_type = X264_TYPE_IDR;
        printf("第一帧为 IDR 帧 (pts=%d)\n", pts);
    }
    else
    {
        enc->pic_in.i_type = X264_TYPE_AUTO;
    }

    /* 拷贝 I420 数据到 x264 输入帧 */
    memcpy(enc->pic_in.img.plane[0], i420, enc->width * enc->height);
    memcpy(enc->pic_in.img.plane[1], i420 + enc->width * enc->height,
           enc->width * enc->height / 4);
    memcpy(enc->pic_in.img.plane[2], i420 + enc->width * enc->height * 5 / 4,
           enc->width * enc->height / 4);
    enc->pic_in.i_pts = pts;

    /* 执行编码 */
    x264_encoder_encode(enc->encoder, nals, nnal, &enc->pic_in, &enc->pic_out);

    /* ===== 提取 SPS/PPS（首次编码时自动提取） ===== */
    if (!enc->sps_pps_extracted && *nnal > 0)
    {
        for (int i = 0; i < *nnal; i++)
        {
            uint8_t *p = (*nals)[i].p_payload;
            int len = (*nals)[i].i_payload;

            /* 跳过 startcode，获取真正的 NALU 数据 */
            int nal_len;
            uint8_t *nal_data = skip_startcode(p, len, &nal_len);
            int nal_type = get_nalu_type(nal_data);

            printf("NALU %d: type=%d, len=%d\n", i, nal_type, nal_len);

            if (nal_type == 7)
            {        /* SPS */
                rtsp_set_sps_pps(nal_data, nal_len, NULL, 0);
                enc->sps_pps_extracted = 1;
                printf("提取到 SPS, 长度=%d\n", nal_len);
            }
            else if (nal_type == 8)
            { /* PPS */
                rtsp_set_sps_pps(NULL, 0, nal_data, nal_len);
                enc->sps_pps_extracted = 1;
                printf("提取到 PPS, 长度=%d\n", nal_len);
            }
        }
    }

    return 0;
}

/**
 * h264_encoder_flush() - 刷出编码器缓存中的剩余帧
 * @enc: 编码器上下文
 * @nals: 输出，NALU 数组
 * @nnal: 输出，NALU 数量
 * Return: 0 成功，<0 失败
 *
 * 当所有输入帧送入后调用，获取编码器内部缓存的剩余数据。
 */
int h264_encoder_flush(h264_encoder_t *enc, x264_nal_t **nals, int *nnal)
{
    return x264_encoder_encode(enc->encoder, nals, nnal, NULL, &enc->pic_out);
}

/**
 * h264_encoder_cleanup() - 释放编码器资源
 * @enc: 编码器上下文
 */
void h264_encoder_cleanup(h264_encoder_t *enc)
{
    x264_encoder_close(enc->encoder);
    x264_picture_clean(&enc->pic_in);
}