/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  main.c
 *    Description:  File-source demo: start RTSP server, push H264 file to VLC via rtsp://
 *                 
 *        Version:  1.0.0(2026/07/27)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/27 11:16:38"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <signal.h>

#include "rtsp_server.h"

int main(int argc, char **argv)
{
    int server_fd;

    // 1. 忽略SIGPIPE信号，防止客户端断开连接时程序崩溃
    signal(SIGPIPE, SIG_IGN);

    printf("\n");
    printf("========================================\n");
    printf("  EdgeRTSP Server v1.0.0\n");
    printf("  Embedded RTSP Streaming Server\n");
    printf("  Copyright (C) 2026 Mayanping\n");
    printf("========================================\n\n");

    // 2. 调用初始化函数，启动RTSP服务器
    server_fd = rtsp_server_init(8554);
    if (server_fd < 0)
    {
        printf("服务器启动失败，程序退出。\n");
        return 1;
    }

    // 3. 运行RTSP服务器，处理客户端请求
    rtsp_server_run(server_fd);

    return 0;
}