/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.h
 *    Description:  RTSP server public API + callback registration for PLAY action
 *                  支持 UDP 和 TCP interleaved 两种传输模式
 *                 
 *        Version:  2.0.0(2026/08/12)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:25:15"
 *                  2, Add TCP interleaved mode support on "2026/08/12"
 *                 
 ********************************************************************************/

#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <stdint.h>

/* ===== 服务器生命周期 ===== */

/**
 * rtsp_server_init() - 初始化 RTSP 服务器
 * @port: 监听端口（通常为 8554）
 *
 * 创建 TCP socket，绑定端口，开始监听。
 * Return: 成功返回 server_fd，失败返回 -1
 */
int rtsp_server_init(int port);

/**
 * rtsp_server_run() - 运行 RTSP 服务器主循环
 * @server_fd: 由 rtsp_server_init() 返回的 socket 描述符
 *
 * 阻塞运行，接受客户端连接并处理 RTSP 请求（OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN）。
 * 内部会根据客户端请求自动启动推流线程。
 */
void rtsp_server_run(int server_fd);


/* ===== 回调注册（推流动作） ===== */

/**
 * rtsp_set_play_action() - 注册 PLAY 时执行的推流函数
 * @action: 函数指针，原型为 void *(*action)(void *)
 *
 * 当 VLC 发送 PLAY 请求时，rtsp_server_run() 会创建线程执行此回调。
 * 回调函数应持续推流，直到 rtsp_is_streaming() 返回 0 时退出。
 *
 * 典型用法：
 *   rtsp_set_play_action(camera_loop);
 */
void rtsp_set_play_action(void *(*action)(void *));


/* ===== 状态查询（供回调函数使用） ===== */

/**
 * rtsp_is_streaming() - 查询当前是否处于推流状态
 * Return: 1=正在推流，0=已停止（PAUSE/TEARDOWN 触发）
 *
 * 由推流线程（camera_loop）定期检查，决定是否继续推流。
 */
int rtsp_is_streaming(void);


/* ===== SPS/PPS 动态注入（由编码器调用） ===== */

/**
 * rtsp_set_sps_pps() - 设置 SPS/PPS 参数集
 * @sps: SPS 数据（不含起始码）
 * @sps_len: SPS 长度
 * @pps: PPS 数据（不含起始码）
 * @pps_len: PPS 长度
 *
 * 编码器在提取到 SPS/PPS 后调用此函数，RTSP 服务器会在 DESCRIBE 响应中
 * 将其 Base64 编码后填入 SDP 的 sprop-parameter-sets 字段。
 *
 * 注意：SPS 和 PPS 必须成对出现，两者都收到后 g_sps_pps_ready 置 1。
 */
void rtsp_set_sps_pps(const uint8_t *sps, int sps_len,
                       const uint8_t *pps, int pps_len);

/**
 * rtsp_is_sps_pps_ready() - 查询 SPS/PPS 是否已就绪
 * Return: 1=已就绪，0=未就绪
 */
int rtsp_is_sps_pps_ready(void);

#endif /* RTSP_SERVER_H */