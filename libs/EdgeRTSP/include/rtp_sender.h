/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_sender.h
 *    Description:  RTP sender public API: rtp_init / rtp_send_nalu / rtp_send_h264_file / rtp_close
 *                  支持 UDP 和 TCP interleaved 两种传输模式
 *                 
 *        Version:  2.0.0(2026/08/12)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:32:02"
 *                  2, Add TCP interleaved mode support on "2026/08/12"
 *                 
 ********************************************************************************/

#ifndef RTP_SENDER_H
#define RTP_SENDER_H

#include <stdint.h>

/* ===== TCP interleaved 通道定义 ===== */
#define RTP_TCP_CHANNEL  0   /* RTP 使用通道 0 */
#define RTCP_TCP_CHANNEL 1   /* RTCP 使用通道 1 */


/* ===== RTP 固定头结构 ===== */

/**
 * struct RTPHeader - RTP 协议固定头（12 字节）
 * @vpxcc: V=2, P=0, X=0, CC=0 → 固定值 0x80
 * @mpt:   M 位（1=帧结束），PT=96（H264 载荷类型）
 * @seq:   序列号（每发送一个 RTP 包 +1）
 * @timestamp: 时间戳（每帧 +3000，即 90000Hz / 30fps）
 * @ssrc:  同步源标识符（会话内固定）
 */
typedef struct {
    uint8_t vpxcc;          /* V=2, P=0, X=0, CC=0 → 0x80 */
    uint8_t mpt;            /* M=0, PT=96（H264）→ 0x60 */
    uint16_t seq;           /* 每包都变 */
    uint32_t timestamp;     /* 每帧都变 */
    uint32_t ssrc;          /* 会话内固定，启动时随机 */
} __attribute__((packed)) RTPHeader;


/* ===== 初始化/销毁 ===== */

/**
 * rtp_init() - UDP 模式 RTP 初始化
 * @client_ip: 客户端 IP 地址（如 "192.168.1.100"）
 * @client_port: 客户端 RTP 端口（由 SETUP 协商）
 * Return: 0 成功，-1 失败
 */
int rtp_init(const char *client_ip, int client_port);

/**
 * rtp_init_tcp() - TCP interleaved 模式 RTP 初始化
 * @rtsp_fd: RTSP 控制连接的文件描述符
 * Return: 0 成功，-1 失败
 *
 * TCP 模式下 RTP 数据通过 RTSP 连接发送，
 * 格式为 $<channel><length><data>。
 */
int rtp_init_tcp(int rtsp_fd);

/**
 * rtp_set_mode() - 设置传输模式
 * @tcp_mode: 0=UDP，1=TCP
 */
void rtp_set_mode(int tcp_mode);

/**
 * rtp_set_rtsp_fd() - 设置 RTSP socket（TCP 模式用）
 * @fd: RTSP 控制连接的文件描述符
 */
void rtp_set_rtsp_fd(int fd);

/**
 * rtp_close() - 关闭 RTP socket，释放资源
 */
void rtp_close(void);


/* ===== 发送接口 ===== */

/**
 * rtp_send_nalu() - 将 H264 NALU 打包成 RTP 包并发送
 * @nalu_data: NALU 数据（不含起始码）
 * @nalu_len: NALU 长度
 *
 * 打包策略：
 * - 小 NALU（≤1400 字节）：单包模式
 * - 大 NALU（>1400 字节）：FU-A 分片模式
 */
void rtp_send_nalu(const uint8_t *nalu_data, int nalu_len);


/* ===== 文件推流（调试/测试用） ===== */

/**
 * rtp_send_h264_file() - 推流本地 H264 文件
 * @filename: H264 文件路径
 *
 * 用于在不连接摄像头时测试 RTP 打包和推流功能。
 * 逐 NALU 发送，并根据 NALU 类型推进时间戳。
 */
void rtp_send_h264_file(const char *filename);


/* ===== 时间戳推进 ===== */

/**
 * rtp_next_frame() - 推进 RTP 时间戳
 * 每编码完一帧调用一次，步长 3000（90000Hz / 30fps）
 */
void rtp_next_frame(void);

#endif /* RTP_SENDER_H */