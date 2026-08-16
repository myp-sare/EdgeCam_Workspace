/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  utils.h
 *    Description:  Utility functions for RTSP request parsing
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:19:39"
 *                 
 ********************************************************************************/

#ifndef UTILS_H
#define UTILS_H

/* ===== RTSP 请求解析工具 ===== */

/**
 * get_vlc_cseq() - 从 RTSP 请求中提取 CSeq 序列号
 * @req: RTSP 请求字符串
 *
 * 查找 "CSeq: " 字段并解析其后的数字。
 * Return: 成功返回 CSeq 值（>=0），失败返回 -1
 */
int get_vlc_cseq(const char *req);

/**
 * parse_client_ports() - 从 SETUP 请求中解析客户端 RTP/RTCP 端口
 * @req: RTSP SETUP 请求字符串
 * @rtp_port: 输出参数，RTP 端口号
 * @rtcp_port: 输出参数，RTCP 端口号（通常为 RTP 端口 + 1）
 *
 * 查找 "client_port=xxxx-yyyy" 格式并提取两个端口号。
 * Return: 0 成功，-1 失败
 */
int parse_client_ports(const char *req, int *rtp_port, int *rtcp_port);

#endif /* UTILS_H */