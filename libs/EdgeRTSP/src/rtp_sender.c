/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_sender.c
 *    Description:  RTP packet sender: init UDP socket, pack NALU into RTP (Single/FU-A)
 *                  支持 UDP 和 TCP interleaved 两种传输模式
 *                 
 *        Version:  2.0.0(2026/08/12)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:22:49"
 *                  2, Add TCP interleaved mode support on "2026/08/12"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rtp_sender.h"

/* ===== 静态变量 ===== */
static int rtp_sockfd = -1;                 /* UDP socket 描述符 */
static int rtsp_fd = -1;                    /* TCP 模式用的 RTSP socket */
static int transport_mode = 0;              /* 0=UDP, 1=TCP */
static struct sockaddr_in dest_addr;        /* RTP 发送目标地址（VLC 的 IP+端口） */
static uint16_t seq = 0;                    /* RTP 序列号，每发送一个包 +1 */
static uint32_t timestamp = 0;              /* RTP 时间戳，每帧 +3000 */
static uint32_t ssrc = 0x12345678;          /* 同步源标识符（固定随机值） */

/* 文件推流（测试/调试用，非实时模式） */
static uint8_t *h264_buffer = NULL;         /* H264 文件缓存指针 */
static int h264_buffer_len = 0;             /* H264 文件总大小 */
static int h264_pos = 0;                    /* 当前读取位置 */

/* ===== 模式设置 ===== */

/**
 * rtp_set_mode() - 设置传输模式
 * @tcp_mode: 0=UDP, 1=TCP
 */
void rtp_set_mode(int tcp_mode)
{
    transport_mode = tcp_mode;
    printf("[RTP] 传输模式: %s\n", tcp_mode ? "TCP (interleaved)" : "UDP");
}

/**
 * rtp_set_rtsp_fd() - 设置 RTSP socket（TCP 模式用）
 * @fd: RTSP 控制连接的文件描述符
 */
void rtp_set_rtsp_fd(int fd)
{
    rtsp_fd = fd;
    printf("[RTP] RTSP fd=%d 已设置，用于 TCP interleaved 模式\n", fd);
}

/* ===== UDP 初始化 ===== */

/**
 * rtp_init() - UDP 模式 RTP 初始化
 * @client_ip: VLC 客户端 IP 地址
 * @client_port: VLC 客户端 RTP 端口
 * Return: 0 成功，-1 失败
 */
int rtp_init(const char *client_ip, int client_port)
{
    /* 1. 创建 UDP socket */
    rtp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (rtp_sockfd < 0)
    {
        perror("rtp socket 创建失败");
        return -1;
    }

    /* 2. 保存目标地址（VLC 的 IP 和 RTP 端口） */
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client_port);
    dest_addr.sin_addr.s_addr = inet_addr(client_ip);
    
    transport_mode = 0;
    seq = 0;
    timestamp = 0;
    
    printf("[RTP] UDP 初始化成功: IP=%s, 端口=%d, sockfd=%d\n", 
           client_ip, client_port, rtp_sockfd);

    return 0;
}

/* ===== TCP interleaved 初始化 ===== */

/**
 * rtp_init_tcp() - TCP interleaved 模式 RTP 初始化
 * @rtsp_fd_param: RTSP 控制连接的文件描述符
 * Return: 0 成功，-1 失败
 */
int rtp_init_tcp(int rtsp_fd_param)
{
    rtsp_fd = rtsp_fd_param;
    transport_mode = 1;
    seq = 0;
    timestamp = 0;
    printf("[RTP] TCP interleaved 初始化成功: rtsp_fd=%d\n", rtsp_fd);

    return 0;
}

/* ===== 发送一个 RTP 包（支持 UDP 和 TCP interleaved） ===== */

/**
 * rtp_send_packet() - 发送一个 RTP 包
 * @payload: RTP payload 数据
 * @payload_len: payload 长度
 * @mark_bit: RTP 头 M 位（1=当前帧的最后一个包）
 *
 * 支持 UDP 和 TCP interleaved 两种传输模式：
 * - UDP: 直接 sendto()
 * - TCP: 添加 $<channel><length> 四字节头部后 send()
 */
static void rtp_send_packet(const uint8_t *payload, int payload_len, int mark_bit)
{
    uint8_t packet[1500];                       /* RTP 包缓冲区（含 12 字节头） */
    RTPHeader *header = (RTPHeader *)packet;    /* 将头部布局到缓冲区开头 */
    
    /* 填充 RTP 固定头：V=2, P=0, X=0, CC=0, PT=96(H264) */
    header->vpxcc = 0x80;
    header->mpt = 0x60 | (mark_bit ? 0x80 : 0x00);
    header->seq = htons(seq++);
    header->timestamp = htonl(timestamp);
    header->ssrc = htonl(ssrc);

    /* 复制 payload 到 RTP 包中（紧跟在 12 字节头之后） */
    memcpy(packet + 12, payload, payload_len);
    int total_size = 12 + payload_len;

    if (transport_mode == 1)
    {
        /* ===== TCP interleaved 模式 ===== */
        /* 格式: $<channel><length-big-endian><data> */
        uint8_t tcp_packet[1504];
        tcp_packet[0] = '$';
        tcp_packet[1] = RTP_TCP_CHANNEL;        /* RTP 通道为 0，RTCP 为 1 */
        tcp_packet[2] = (total_size >> 8) & 0xFF;
        tcp_packet[3] = total_size & 0xFF;
        memcpy(tcp_packet + 4, packet, total_size);
        
        int tcp_total = 4 + total_size;
        ssize_t sent = send(rtsp_fd, tcp_packet, tcp_total, 0);
        
        if (sent < 0)
        {
            perror("[RTP] TCP send 失败");
            return;
        }
        
        printf("[RTP] TCP包: seq=%d, timestamp=%u, size=%d (通道=%d)\n",
               seq - 1, timestamp, total_size, RTP_TCP_CHANNEL);
    }
    else
    {
        /* ===== UDP 模式 ===== */
        if (rtp_sockfd < 0)
        {
            printf("[RTP] UDP socket 未初始化，无法发送。\n");
            return;
        }

        ssize_t sent = sendto(rtp_sockfd, packet, total_size, 0,
                              (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent < 0)
        {
            perror("[RTP] UDP sendto 失败");
            return;
        }

        printf("[RTP] UDP包: seq=%d, timestamp=%u, size=%d\n",
               seq - 1, timestamp, total_size);
    }
}

/* ===== 时间戳推进 ===== */

/**
 * rtp_next_frame() - 推进 RTP 时间戳
 * 每编码完一帧调用一次，步长 3000（90000Hz / 30fps）
 */
void rtp_next_frame(void)
{
    timestamp += 3000;   /* 90000 / 30fps = 3000 */
}

/* ===== NALU 打包与发送（核心接口） ===== */

/**
 * rtp_send_nalu() - 将 H264 NALU 打包成 RTP 包并发送
 * @nalu_data: NALU 数据（不含起始码）
 * @nalu_len: NALU 长度
 *
 * 打包策略：
 * - 小 NALU（≤1400 字节）：单包模式，直接放入 RTP payload
 * - 大 NALU（>1400 字节）：FU-A 分片模式，拆成多个 RTP 包
 *
 * FU-A 分片格式：
 *   [RTP Header][FU Indicator][FU Header][分片数据]
 *   - FU Indicator: Type=28, 保留 F/NRI
 *   - FU Header: S=1 首片, E=1 尾片, Type=原始 NALU 类型
 */
void rtp_send_nalu(const uint8_t *nalu_data, int nalu_len)
{
    if (nalu_data == NULL || nalu_len <= 0)
    {
        return;
    }

    /* 减少打印，避免影响性能 */
    /* printf("[RTP] 发送 NALU, len=%d\n", nalu_len); */

    int max_payload = 1400 - 12;                /* 1400 - RTP头 = 最大 payload */
    uint8_t nal_type = nalu_data[0] & 0x1F;     /* NALU 类型（5=IDR, 1=P帧, 7=SPS, 8=PPS） */

    /* 小 NALU：单包模式，M 位=1（帧结束） */
    if (nalu_len <= max_payload)
    {
        rtp_send_packet(nalu_data, nalu_len, 1);
        return;
    }

    /* ===== 大 NALU：FU-A 分片模式 ===== */
    int max_fu_payload = max_payload - 2;       /* 减去 FU Indicator 和 FU Header */
    int payload_offset = 1;                     /* 跳过 NALU 的第一个字节（原始头） */
    int remaining = nalu_len - 1;               /* 剩余待发送数据量（不含 NALU 头） */
    int is_first = 1;                           /* 是否为首片 */

    while (remaining > 0)
    {
        int chunk = (remaining > max_fu_payload) ? max_fu_payload : remaining;
        int is_last = (chunk == remaining);     /* 是否为尾片 */

        uint8_t fu_packet[1500];
        RTPHeader *header = (RTPHeader *)fu_packet;
        
        header->vpxcc = 0x80;
        header->mpt = 0x60 | (is_last ? 0x80 : 0x00);  /* 尾片 M=1，中间片 M=0 */
        header->seq = htons(seq++);
        header->timestamp = htonl(timestamp);
        header->ssrc = htonl(ssrc);

        /* FU Indicator：保留原始 NALU 的 F 和 NRI，Type 固定为 28 (FU-A) */
        fu_packet[12] = (nalu_data[0] & 0xE0) | 28;

        /* FU Header：S/E 位 + 原始 NALU 类型 */
        fu_packet[13] = 0;

        if (is_first){
            fu_packet[13] |= 0x80;    /* S=1: 首片 */
        }
        if (is_last){
            fu_packet[13] |= 0x40;    /* E=1: 尾片 */
        }

        fu_packet[13] |= nal_type;

        memcpy(fu_packet + 14, nalu_data + payload_offset, chunk);
        int total_size = 12 + 2 + chunk;

        /* 发送分片（支持 UDP 和 TCP） */
        if (transport_mode == 1)
        {
            /* TCP interleaved */
            uint8_t tcp_packet[1504];
            tcp_packet[0] = '$';
            tcp_packet[1] = RTP_TCP_CHANNEL;
            tcp_packet[2] = (total_size >> 8) & 0xFF;
            tcp_packet[3] = total_size & 0xFF;
            memcpy(tcp_packet + 4, fu_packet, total_size);
            
            ssize_t sent = send(rtsp_fd, tcp_packet, 4 + total_size, 0);
            if (sent < 0)
            {
                perror("[RTP] TCP FU-A send 失败");
                return;
            }
            
            printf("[RTP] TCP FU-A分片: seq=%d, S=%d, E=%d, size=%d\n",
                   seq - 1, is_first, is_last, total_size);
        }
        else
        {
            /* UDP */
            if (rtp_sockfd < 0)
            {
                printf("[RTP] UDP socket 未初始化\n");
                return;
            }
            
            ssize_t sent = sendto(rtp_sockfd, fu_packet, total_size, 0,
                                  (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (sent < 0)
            {
                perror("[RTP] UDP FU-A sendto 失败");
                return;
            }
            
            printf("[RTP] UDP FU-A分片: seq=%d, S=%d, E=%d, size=%d\n",
                   seq - 1, is_first, is_last, total_size);
        }

        payload_offset += chunk;
        remaining -= chunk;
        is_first = 0;                           /* 首片之后，后续片 S=0 */
    }
}

/* ===== 加载 H.264 文件到内存（文件推流用） ===== */

/**
 * load_h264_file() - 加载 H264 文件到内存
 * @filename: H264 文件路径
 * Return: 0 成功，-1 失败
 */
static int load_h264_file(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        printf("[RTP] 文件打开失败: %s\n", filename);
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    h264_buffer_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    h264_buffer = malloc(h264_buffer_len);
    if (!h264_buffer)
    {
        printf("[RTP] 内存分配失败！\n");
        fclose(fp);
        return -1;
    }

    size_t read_len = fread(h264_buffer, 1, h264_buffer_len, fp);
    fclose(fp);

    if (read_len != (size_t)h264_buffer_len)
    {
        printf("[RTP] 读取文件失败: 期望 %d, 实际 %zu\n", h264_buffer_len, read_len);
        free(h264_buffer);
        h264_buffer = NULL;
        return -1;
    }

    h264_pos = 0;
    printf("[RTP] 获取H.264裸流成功: %s, 大小=%d 字节\n", filename, h264_buffer_len);

    return 0;
}

/* ===== 从 H264 裸流提取 NALU（文件推流用） ===== */

/**
 * get_next_nalu() - 从文件缓存中提取下一个 NALU
 * @nalu_start: 输出，NALU 数据起始指针
 * @nalu_len: 输出，NALU 数据长度
 *
 * 查找 4 字节起始码 (00 00 00 01) 或 3 字节起始码 (00 00 01)，
 * 返回跳过起始码后的 NALU 数据。
 * Return: 0 成功，-1 已无更多数据
 */
static int get_next_nalu(uint8_t **nalu_start, int *nalu_len)
{
    if (h264_pos >= h264_buffer_len)
    {
        return -1;
    }
    
    /* 1. 查找起始码 */
    int start_code_len = 0;
    if (h264_pos + 4 <= h264_buffer_len &&
        h264_buffer[h264_pos] == 0x00 &&
        h264_buffer[h264_pos + 1] == 0x00 &&
        h264_buffer[h264_pos + 2] == 0x00 &&
        h264_buffer[h264_pos + 3] == 0x01)
    {
        start_code_len = 4;
    }
    else if (h264_pos + 3 <= h264_buffer_len &&
             h264_buffer[h264_pos] == 0x00 &&
             h264_buffer[h264_pos + 1] == 0x00 &&
             h264_buffer[h264_pos + 2] == 0x01)
    {
        start_code_len = 3;
    }
    else
    {
        printf("[RTP] 在位置 %d 找不到起始码\n", h264_pos);
        h264_pos++;
        return -1;
    }
    
    /* 2. 跳过起始码 */
    int nalu_start_pos = h264_pos + start_code_len;
    
    /* 3. 查找下一个起始码 */
    int next_start = -1;
    for (int i = nalu_start_pos + 1; i < h264_buffer_len; i++)
    {
        if (i + 4 <= h264_buffer_len &&
            h264_buffer[i] == 0x00 &&
            h264_buffer[i + 1] == 0x00 &&
            h264_buffer[i + 2] == 0x00 &&
            h264_buffer[i + 3] == 0x01)
        {
            next_start = i;
            break;
        }
        if (i + 3 <= h264_buffer_len &&
            h264_buffer[i] == 0x00 &&
            h264_buffer[i + 1] == 0x00 &&
            h264_buffer[i + 2] == 0x01)
        {
            next_start = i;
            break;
        }
    }
    
    /* 4. 计算 NALU 长度（到下一个起始码之前） */
    if (next_start == -1)
    {
        *nalu_len = h264_buffer_len - nalu_start_pos;
    }
    else
    {
        *nalu_len = next_start - nalu_start_pos;
    }
    
    *nalu_start = h264_buffer + nalu_start_pos;
    h264_pos = (next_start == -1) ? h264_buffer_len : next_start;
    
    return 0;
}

/* ===== 推流 H.264 文件（调试/测试用） ===== */

/**
 * rtp_send_h264_file() - 推流本地 H264 文件
 * @filename: H264 文件路径
 *
 * 用于在不连接摄像头时测试 RTP 打包和推流功能。
 * 逐 NALU 发送，并根据 NALU 类型（IDR/P帧）推进时间戳。
 */
void rtp_send_h264_file(const char *filename)
{
    /* 1. 加载文件 */
    if (load_h264_file(filename) < 0)
    {
        printf("[RTP] 文件加载失败，停止推流。\n");
        return;
    }

    /* 2. 重置序号和时间戳 */
    seq = 0;
    timestamp = 0;

    /* 3. 循环发送 */
    uint8_t *nalu = NULL;
    int nalu_len = 0;
    int frame_count = 0;
    int nalu_count = 0;
    int sps_count = 0, pps_count = 0, idr_count = 0, sei_count = 0, slice_count = 0;

    while (get_next_nalu(&nalu, &nalu_len) == 0)
    {
        uint8_t nal_type = nalu[0] & 0x1F;
        nalu_count++;

        /* 统计每种 NALU 的出现次数 */
        if (nal_type == 7){
            sps_count++;
        }
        else if (nal_type == 8)
        {
            pps_count++;
        }
        else if (nal_type == 5)
        {
            idr_count++;
        }
        else if (nal_type == 6)
        {
            sei_count++;
        }
        else if (nal_type == 1)
        {
            slice_count++;
        }

        /* 打印前 5 条 NALU 信息（便于调试） */
        if (nalu_count <= 5)
        {
            printf("[RTP] %d. type=%d, len=%d\n", nalu_count, nal_type, nalu_len);
        }

        /* 发送 NALU */
        rtp_send_nalu(nalu, nalu_len);

        /* 时间戳推进：每处理完一帧（IDR 或 P 帧）推进一次 */
        if (nal_type == 1 || nal_type == 5)
        {
            rtp_next_frame();
            frame_count++;
            usleep(33000);   /* 约 30fps 节奏，避免瞬间发完 */
        }
    }

    printf("\n===== 推流完成 =====\n");
    printf("NALU总数: %d\n", nalu_count);
    printf("帧总数: %d\n", frame_count);
    printf("SPS=%d, PPS=%d, IDR=%d, SEI=%d, Slice=%d\n",
           sps_count, pps_count, idr_count, sei_count, slice_count);
    printf("====================\n\n");

    free(h264_buffer);
    h264_buffer = NULL;
}

/* ===== 关闭 RTP ===== */

/**
 * rtp_close() - 关闭 RTP socket，释放资源
 */
void rtp_close(void)
{
    if (rtp_sockfd >= 0)
    {
        close(rtp_sockfd);
        rtp_sockfd = -1;
    }
    rtsp_fd = -1;
    printf("[RTP] RTP socket 已关闭。\n");
}