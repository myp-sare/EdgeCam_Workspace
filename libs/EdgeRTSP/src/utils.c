/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  utils.c
 *    Description:  RTSP request helpers: parse CSeq, parse client ports from SETUP
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:18:40"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <string.h>
#include "utils.h"

/* ===== CSeq 解析 ===== */

/**
 * get_vlc_cseq() - 从 RTSP 请求中提取 CSeq 序列号
 * @req: RTSP 请求字符串
 * Return: 成功返回 CSeq 值，失败返回 -1
 *
 * RTSP 协议中每个请求都包含 CSeq 字段，响应必须携带相同的 CSeq。
 */
int get_vlc_cseq(const char *req)
{
    const char *cseq_str = strstr(req, "CSeq: ");
    if (!cseq_str)
    {
        return -1;      /* 未找到 CSeq 字段 */
    }
    int cseq;
    if (sscanf(cseq_str, "CSeq: %d", &cseq) == 1)
    {
        return cseq;    /* 解析成功 */
    }

    return -1;          /* 解析失败 */
}


/* ===== 客户端端口解析 ===== */

/**
 * parse_client_ports() - 从 SETUP 请求中解析客户端 RTP/RTCP 端口
 * @req: RTSP SETUP 请求字符串
 * @rtp_port: 输出参数，RTP 端口
 * @rtcp_port: 输出参数，RTCP 端口
 * Return: 0 成功，-1 失败
 *
 * SETUP 请求的 Transport 头中携带 client_port=rtp-rtcp，
 * 例如：Transport: RTP/AVP;unicast;client_port=9014-9015
 */
int parse_client_ports(const char *req, int *rtp_port, int *rtcp_port)
{
    const char *vlc_port = strstr(req, "client_port=");
    if (!vlc_port)
    {
        return -1;      /* 未找到 client_port 字段 */
    }
    if (sscanf(vlc_port, "client_port=%d-%d", rtp_port, rtcp_port) == 2)
    {
        return 0;       /* 解析成功 */
    }
    
    return -1;          /* 解析失败 */
}