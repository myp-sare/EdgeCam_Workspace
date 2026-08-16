/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.h
 *    Description:  MPP 硬件编码器封装（RV1106 端）
 *                  输入 NV12 → 输出 H264 裸流（vendor packet ABI）
 *                  与 PC 端 x264 软件编码器形成对比：
 *                  - PC 端：x264 软件编码，输出标准 Annex B 格式
 *                  - RV1106：MPP 硬件编码，通过 vendor packet 取流
 *                 
 *        Version:  1.0.0(2026/08/05)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/08/05 08:44:02"
 *                 
 ********************************************************************************/

#ifndef MPP_ENCODER_H
#define MPP_ENCODER_H

#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_mpi.h>

/**
 * struct MppEncoder - MPP 编码器上下文
 * @ctx: MPP 上下文句柄
 * @mpi: MPP API 函数表
 * @group: 缓冲区组（通常为 NULL，使用默认分配器）
 * @frm_buf: 输入帧缓冲区（存放 NV12 数据）
 * @pkt_buf: 辅助缓冲区（内部使用）
 *
 * @width: 图像宽度
 * @height: 图像高度
 * @fps: 帧率
 * @gop: GOP 大小（每 N 帧一个 IDR）
 * @bit_rate: 目标码率（bps）
 * @bit_rate_min: 最小码率（bps）
 * @bit_rate_max: 最大码率（bps）
 * @frame_size: 单帧 NV12 大小（visible）
 * @frame_index: 帧序号（用于 PTS）
 *
 * @initialized: 初始化标志
 * @header_pending: 头信息待发送标志
 * @header_data: 头信息数据
 * @header_len: 头信息长度
 */
typedef struct {
    MppCtx ctx;                     /* MPP 上下文句柄 */
    MppApi *mpi;                    /* MPP API 函数表 */
    MppBufferGroup group;           /* 缓冲区组（NULL = 使用默认分配器） */
    MppBuffer frm_buf;              /* 输入帧缓冲区（NV12） */
    MppBuffer pkt_buf;              /* 辅助缓冲区（内部使用） */

    int width;                      /* 图像宽度 */
    int height;                     /* 图像高度 */
    int fps;                        /* 帧率 */
    int gop;                        /* GOP 大小（每 N 帧一个 IDR） */
    int bit_rate;                   /* 目标码率 (bps) */
    int bit_rate_min;               /* 最小码率 (bps) */
    int bit_rate_max;               /* 最大码率 (bps) */
    size_t frame_size;              /* 单帧 NV12 大小 (visible) */
    int64_t frame_index;            /* 帧序号（用于 PTS） */

    int initialized;                /* 初始化标志 (1=已初始化) */
    int header_pending;             /* 头信息待发送标志 */
    uint8_t *header_data;           /* 头信息数据 */
    int header_len;                 /* 头信息长度 */
} MppEncoder;

/* ===== 编码器生命周期 ===== */

/**
 * mpp_encoder_init() - 使用默认参数初始化 MPP 编码器
 * @encoder: 编码器上下文
 * @width: 图像宽度
 * @height: 图像高度
 *
 * 默认参数：fps=30, gop=30, bitrate=4Mbps
 * Return: 0 成功，-1 失败
 */
int mpp_encoder_init(MppEncoder *encoder, int width, int height);

/**
 * mpp_encoder_init_ex() - 使用自定义参数初始化 MPP 编码器
 * @encoder: 编码器上下文
 * @width: 图像宽度
 * @height: 图像高度
 * @fps: 帧率
 * @gop: GOP 大小（每 N 帧一个 IDR）
 * @bit_rate: 目标码率 (bps)
 *
 * 完整初始化流程：
 *   1. 分配输入缓冲区（MppBuffer，物理连续）
 *   2. mpp_create() 创建上下文
 *   3. mpp_init_ext() 初始化编码器
 *   4. 配置编码参数（CBR + GOP + 帧率）
 * Return: 0 成功，-1 失败
 */
int mpp_encoder_init_ex(MppEncoder *encoder, int width, int height,
                        int fps, int gop, int bit_rate);

/**
 * mpp_encoder_encode() - 编码一帧 NV12 图像（硬件加速）
 * @encoder: 编码器上下文
 * @nv12: NV12 数据指针（用户空间虚拟地址）
 * @size: 输入数据大小（应为 frame_size）
 * @out: 输出参数，指向 H264 数据的指针（调用者负责 free）
 *
 * 编码流程：
 *   1. 拷贝 NV12 数据到 MppBuffer（物理连续内存）
 *   2. 封装为 MppFrame
 *   3. encode_put_frame() 送入编码器（异步）
 *   4. encode_get_packet() 获取 venc_packet 描述符
 *   5. copy_vendor_packet() 从环形缓冲区拷贝 H264 数据
 *   6. encode_release_packet() 释放环形缓冲区资源
 *
 * 注意：前几帧可能无输出（编码器需要积累参考帧）。
 * Return: 成功返回 H264 数据长度，-1 失败，-2 无输出
 */
int mpp_encoder_encode(MppEncoder *encoder, const uint8_t *nv12,
                       int size, uint8_t **out);

/**
 * mpp_encoder_close() - 关闭编码器，释放所有资源
 * @encoder: 编码器上下文
 *
 * 清理顺序：reset → mpp_destroy → mpp_buffer_put
 */
void mpp_encoder_close(MppEncoder *encoder);

#endif /* MPP_ENCODER_H */