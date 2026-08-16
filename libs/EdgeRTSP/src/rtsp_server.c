/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.c
 *    Description:  Minimal RTSP server: handle OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN, callback-based stream source
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:23:00"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>     /* socket, bind, listen, accept */
#include <netinet/in.h>     /* struct sockaddr_in */
#include <arpa/inet.h>      /* inet_ntoa, htons */
#include <pthread.h>

#include "rtsp_server.h"
#include "rtp_sender.h"
#include "utils.h"

#define BUF_SIZE 4096

/* ===== 传输模式（UDP/TCP） ===== */
static int transport_mode = 0;   /* 0=UDP, 1=TCP (interleaved) */


/* ===== SPS/PPS 存储 ===== */

static char g_sps_base64[128] = {0};        /* Base64 编码后的 SPS */
static char g_pps_base64[128] = {0};        /* Base64 编码后的 PPS */
static int g_sps_pps_ready = 0;             /* 1=已就绪 */
static pthread_mutex_t g_sps_mutex = PTHREAD_MUTEX_INITIALIZER;  /* 保护 SPS/PPS 的互斥锁 */


/* ===== Base64 编码 ===== */

/**
 * base64_encode() - Base64 编码
 * @input: 输入数据
 * @len: 输入数据长度
 * @output: 输出字符串（调用者保证足够空间）
 *
 * 用于将二进制 SPS/PPS 转为 SDP 所需的 Base64 字符串。
 */
static void base64_encode(const uint8_t *input, int len, char *output)
{
    const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    uint8_t a, b, c;

    while (i < len)
    {
        a = input[i++];
        b = (i < len) ? input[i++] : 0;
        c = (i < len) ? input[i++] : 0;

        output[j++] = table[a >> 2];
        output[j++] = table[((a & 0x03) << 4) | (b >> 4)];
        output[j++] = (i - 2 < len) ? table[((b & 0x0f) << 2) | (c >> 6)] : '=';
        output[j++] = (i - 1 < len) ? table[c & 0x3f] : '=';
    }
    output[j] = '\0';
}


/* ===== SPS/PPS 公共接口 ===== */

/**
 * rtsp_set_sps_pps() - 设置 SPS/PPS 参数集
 * @sps: SPS 数据（不含起始码）
 * @sps_len: SPS 长度
 * @pps: PPS 数据（不含起始码）
 * @pps_len: PPS 长度
 *
 * 编码器提取到 SPS/PPS 后调用。两者都收到后 g_sps_pps_ready = 1。
 */
void rtsp_set_sps_pps(const uint8_t *sps, int sps_len,
                       const uint8_t *pps, int pps_len)
{
    pthread_mutex_lock(&g_sps_mutex);

    if (sps != NULL && sps_len > 0)
    {
        memset(g_sps_base64, 0, sizeof(g_sps_base64));
        base64_encode(sps, sps_len, g_sps_base64);
    }
    if (pps != NULL && pps_len > 0)
    {
        memset(g_pps_base64, 0, sizeof(g_pps_base64));
        base64_encode(pps, pps_len, g_pps_base64);
    }

    if (strlen(g_sps_base64) > 0 && strlen(g_pps_base64) > 0)
    {
        g_sps_pps_ready = 1;
        printf("SPS/PPS 已完整就绪!\n");
    }

    pthread_mutex_unlock(&g_sps_mutex);
}

/**
 * rtsp_is_sps_pps_ready() - 查询 SPS/PPS 是否已就绪
 * Return: 1=就绪，0=未就绪
 */
int rtsp_is_sps_pps_ready(void)
{
    return g_sps_pps_ready;
}


/* ===== 推流控制状态 ===== */

static volatile int g_streaming = 0;        /* 流状态：0=停止，1=播放中 */
static pthread_t g_stream_tid;              /* 推流线程 ID（用于 join） */
static void *(*play_action)(void *) = NULL; /* PLAY 回调函数指针 */
static int client_rtp_port = 0;             /* 客户端 RTP 端口 */
static int client_rtcp_port = 0;            /* 客户端 RTCP 端口（通常 RTP+1） */
static char client_ip[64] = {0};            /* 客户端 IP 地址 */


/* ===== 回调注册与状态查询 ===== */

void rtsp_set_play_action(void *(*action)(void *))
{
    play_action = action;
}

int rtsp_is_streaming(void)
{
    return g_streaming;
}


/* ===== RTSP 请求处理函数 ===== */

/**
 * send_options() - 响应 OPTIONS 请求
 * @client_fd: 客户端 socket
 * @cseq: CSeq 序列号
 *
 * 返回服务器支持的 RTSP 方法列表。
 */
static void send_options(int client_fd, int cseq)
{
    char response[512];

    snprintf(response, sizeof(response),
             "RTSP/1.0 200 OK\r\n"
             "CSeq: %d\r\n"
             "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN\r\n"
             "\r\n",
             cseq);

    send(client_fd, response, strlen(response), 0);
    printf("已发送 OPTIONS 响应: \n%s", response);
}

/**
 * send_describe() - 响应 DESCRIBE 请求
 * @client_fd: 客户端 socket
 * @cseq: CSeq 序列号
 *
 * 返回 SDP 描述，包含视频参数和 SPS/PPS。
 * 如果 SPS/PPS 尚未就绪，会等待最多 2 秒，超时则使用占位符。
 */
static void send_describe(int client_fd, int cseq)
{
    char sps_pps[512];
    char sdp[2048];
    int wait_count = 0;

    /* 等待 SPS/PPS 就绪（最多等待 2 秒） */
    while (!g_sps_pps_ready && wait_count < 100)
    {
        usleep(50000);
        wait_count++;
    }

    pthread_mutex_lock(&g_sps_mutex);

    if (g_sps_pps_ready && strlen(g_sps_base64) > 0 && strlen(g_pps_base64) > 0)
    {
        snprintf(sps_pps, sizeof(sps_pps),
                 "sprop-parameter-sets=%s,%s",
                 g_sps_base64, g_pps_base64);
        printf("使用动态SPS/PPS (等待%dms)\n", wait_count * 50);
    }
    else
    {
        /* 超时使用占位符（解码器会尝试从码流中获取） */
        snprintf(sps_pps, sizeof(sps_pps),
                 "sprop-parameter-sets=Z2QAKKzN2QFAFuaAQCAAAAMAAQAAAwA8h4UYAQ==,aO48sA==");
        printf("SPS/PPS超时, 使用占位符\n");
    }

    pthread_mutex_unlock(&g_sps_mutex);

    /* 构造 SDP */
    snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=EdgeRTSP\r\n"
        "t=0 0\r\n"
        "a=control:*\r\n"
        "a=range:npt=0-\r\n"
        "m=video 0 RTP/AVP 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 packetization-mode=1;%s\r\n"
        "a=control:track1\r\n",
        sps_pps);

    char response[4096];
    int response_len = snprintf(response, sizeof(response),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        cseq,
        strlen(sdp),
        sdp);

    send(client_fd, response, response_len, 0);
    printf("已发送 DESCRIBE 响应 (SDP长度=%zu)\n", strlen(sdp));
}

/**
 * send_setup() - 响应 SETUP 请求
 * @client_fd: 客户端 socket
 * @cseq: CSeq 序列号
 * @buf: 完整请求消息（用于解析 Transport 头）
 *
 * 解析客户端 Transport 头，决定使用 UDP 还是 TCP interleaved 模式，
 * 保存客户端 IP/端口，用于后续 RTP 初始化。
 */
static void send_setup(int client_fd, int cseq, const char *buf)
{
    char response[512];

    /* 检查客户端是否请求 TCP 模式 */
    const char *transport = strstr(buf, "Transport:");
    int use_tcp = 0;

    if (transport && strstr(transport, "TCP") != NULL)
    {
        use_tcp = 1;
    }

    if (use_tcp)
    {
        /* ===== TCP interleaved 模式 ===== */
        rtp_set_mode(1);
        rtp_set_rtsp_fd(client_fd);

        snprintf(response, sizeof(response),
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Session: 12345678\r\n"
            "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n"
            "\r\n",
            cseq);
        printf("已发送 SETUP 响应（TCP interleaved 模式）: CSeq=%d\n", cseq);
    }
    else
    {
        /* ===== UDP 模式 ===== */
        rtp_set_mode(0);

        snprintf(response, sizeof(response),
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Session: 12345678\r\n"
            "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=6000-6001\r\n"
            "\r\n",
            cseq,
            client_rtp_port,
            client_rtcp_port);
        printf("已发送 SETUP 响应（UDP 模式）: CSeq=%d, client_port=%d-%d\n",
               cseq, client_rtp_port, client_rtcp_port);
    }

    send(client_fd, response, strlen(response), 0);
}

/**
 * send_play() - 响应 PLAY 请求
 * @client_fd: 客户端 socket
 * @cseq: CSeq 序列号
 *
 * 返回 200 OK，并携带 RTP-Info（初始 seq=0, rtptime=0）。
 * 外部调用者会在此之后初始化 RTP 并启动推流线程。
 */
static void send_play(int client_fd, int cseq)
{
    char response[512];
    snprintf(response, sizeof(response),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Session: 12345678\r\n"
        "RTP-Info: url=rtsp://0.0.0.0:8554/live/track1;seq=0;rtptime=0\r\n"
        "\r\n",
        cseq);

    send(client_fd, response, strlen(response), 0);
    printf("已发送 PLAY 响应: \nCSeq=%d, nRTP-Info: seq=0, rtptime=0\n\n", cseq);
}


/* ===== RTSP 服务器生命周期 ===== */

/**
 * rtsp_server_init() - 初始化 RTSP 服务器
 * @port: 监听端口
 * Return: server_fd 成功，-1 失败
 */
int rtsp_server_init(int port)
{
    int server_fd;
    struct sockaddr_in server_addr;
    int opt = 1;

    /* 1. 创建 TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket 创建失败");
        return -1;
    }

    /* 2. SO_REUSEADDR：防止 TIME_WAIT 导致端口无法立即复用 */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt 设置失败");
        close(server_fd);
        return -1;
    }

    /* 3. 绑定地址 */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind 绑定失败");
        close(server_fd);
        return -1;
    }

    /* 4. 开始监听 */
    if (listen(server_fd, 3) < 0)
    {
        perror("listen 监听失败");
        close(server_fd);
        return -1;
    }

    printf("RTSP 服务器启动成功正在监听端口 %d\n", port);
    return server_fd;
}

/**
 * rtsp_server_run() - 运行 RTSP 服务器主循环
 * @server_fd: 监听 socket
 *
 * 阻塞运行，持续接受客户端连接并处理 RTSP 请求。
 * 收到 PLAY 后启动推流线程；收到 TEARDOWN 后停止推流。
 */
void rtsp_server_run(int server_fd)
{
    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);
    char buf[BUF_SIZE];

    printf("RTSP 服务器已就绪, 等待 VLC 连接...\n");

    while (1)
    {
        /* 1. 等待客户端连接 */
        int client_fd;
        client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);
        if (client_fd < 0)
        {
            perror("accept 失败");
            continue;
        }

        /* 保存客户端 IP（供 RTP 初始化使用） */
        strncpy(client_ip, inet_ntoa(client_address.sin_addr), sizeof(client_ip) - 1);
        client_ip[sizeof(client_ip) - 1] = '\0';

        printf("VLC已连接: \nIP=%s, 端口=%d\n\n",
               inet_ntoa(client_address.sin_addr),
               ntohs(client_address.sin_port));

        /* 2. 处理 RTSP 请求循环 */
        while (1)
        {
            memset(buf, 0, sizeof(buf));
            int read_len = recv(client_fd, buf, sizeof(buf) - 1, 0);
            if (read_len <= 0)
            {
                printf("VLC已断开连接, 等待下一个客户端...\n");
                break;
            }

            buf[read_len] = '\0';
            printf("收到 RTSP 请求:\n%s", buf);

            /* 解析 CSeq */
            int cseq = get_vlc_cseq(buf);
            if (cseq < 0)
            {
                printf("未找到 CSeq, 跳过该请求。\n");
                continue;
            }

            /* ===== 路由到对应处理函数 ===== */
            if (strstr(buf, "OPTIONS"))
            {
                send_options(client_fd, cseq);

            }
            else if (strstr(buf, "DESCRIBE"))
            {
                send_describe(client_fd, cseq);

            }
            else if (strstr(buf, "SETUP"))
            {
                /* 解析客户端端口号（UDP 模式需要） */
                if (parse_client_ports(buf, &client_rtp_port, &client_rtcp_port) < 0)
                {
                    char bad_request[128];
                    snprintf(bad_request, sizeof(bad_request),
                        "RTSP/1.0 400 bad_request\r\nCSeq: %d\r\n\r\n", cseq);
                    send(client_fd, bad_request, strlen(bad_request), 0);
                    continue;
                }
                send_setup(client_fd, cseq, buf);

            }
            else if (strstr(buf, "PLAY"))
            {
                g_streaming = 1;
                send_play(client_fd, cseq);

                /* 根据传输模式初始化 RTP */
                if (transport_mode == 1)
                {
                    if (rtp_init_tcp(client_fd) < 0)
                    {
                        printf("[RTSP] RTP TCP 初始化失败\n");
                        g_streaming = 0;
                        continue;
                    }
                }
                else
                {
                    if (rtp_init(client_ip, client_rtp_port) < 0)
                    {
                        printf("[RTSP] RTP UDP 初始化失败\n");
                        g_streaming = 0;
                        continue;
                    }
                }

                /* 启动推流线程（回调由用户注册） */
                if (play_action != NULL)
                {
                    pthread_create(&g_stream_tid, NULL, play_action, NULL);
                }
                else
                {
                    /* 调试模式：直接推文件 */
                    rtp_send_h264_file("/home/mayanping/workspace/EdgeRTSP/output.h264");
                }

            }
            else if (strstr(buf, "PAUSE"))
            {
                printf("VLC 请求暂停播放...\n");
                g_streaming = 0;
                char response[128];
                snprintf(response, sizeof(response),
                        "RTSP/1.0 200 OK\r\nCSeq: %d\r\n\r\n", cseq);
                send(client_fd, response, strlen(response), 0);

            }
            else if (strstr(buf, "TEARDOWN"))
            {
                printf("VLC 请求断开连接...\n");

                g_streaming = 0;
                if (play_action != NULL)
                {
                    pthread_join(g_stream_tid, NULL);   /* 等待推流线程退出 */
                }
                rtp_close();
                break;

            }
            else
            {
                printf("未知请求类型,暂不处理。\n");
                char not_found[128];
                snprintf(not_found, sizeof(not_found),
                        "RTSP/1.0 404 Not Found\r\nCSeq: %d\r\n\r\n\n", cseq);
                send(client_fd, not_found, strlen(not_found), 0);
            }
        }

        close(client_fd);
        printf("VLC已断开连接, 等待下一个客户端...\n\n");
    }
}