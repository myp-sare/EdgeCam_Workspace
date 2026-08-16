/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  pixel_convert.c
 *    Description:  像素格式转换实现（PC 端）
 *                  YUYV → I420（x264 编码输入）
 *                  YUYV → RGB24（预览/OpenCV 用）
 *                  RGB24 → BMP（调试用图片保存）
 *                 
 *        Version:  1.0.0(2026/07/01)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/01 10:57:09"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pixel_convert.h"


/* ===== YUYV → I420（供 x264 编码使用） ===== */

/**
 * yuyv_to_i420() - YUYV 4:2:2 → I420 4:2:0
 * @yuyv: 输入 YUYV 数据（Y-U-Y-V 交错排列）
 * @i420: 输出 I420 数据（Y 平面 + U 平面 + V 平面连续存储）
 * @w: 图像宽度
 * @h: 图像高度
 *
 * YUYV 格式：每 2 个像素共享一组 U/V
 *   [Y0][U0][Y1][V0] [Y2][U1][Y3][V1] ...
 *
 * I420 格式：
 *   Y 平面: w × h 字节（全采样）
 *   U 平面: (w/2) × (h/2) 字节（每 2×2 像素取一个 U）
 *   V 平面: (w/2) × (h/2) 字节（每 2×2 像素取一个 V）
 */
void yuyv_to_i420(const uint8_t *yuyv, uint8_t *i420, int w, int h)
{
    int y_size = w * h;
    uint8_t *y_plane = i420;                          /* Y 平面起始 */
    uint8_t *u_plane = i420 + y_size;                 /* U 平面起始 */
    uint8_t *v_plane = i420 + y_size + y_size / 4;    /* V 平面起始 */

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int yi = (y * w + x) * 2;                 /* YUYV 中的偏移 */
            y_plane[y * w + x] = yuyv[yi];            /* Y 全采样 */

            /* U/V 只在偶数行、偶数列采样（4:2:0 下采样） */
            if ((y % 2 == 0) && (x % 2 == 0))
            {
                int uv_idx = (y / 2) * (w / 2) + (x / 2);
                u_plane[uv_idx] = yuyv[yi + 1];       /* U */
                v_plane[uv_idx] = yuyv[yi + 3];       /* V */
            }
        }
    }
}


/* ===== YUYV → RGB24（预览/OpenCV 用） ===== */

/**
 * yuyv_to_rgb() - YUYV 4:2:2 → RGB24
 * @yuyv: 输入 YUYV 数据
 * @rgb: 输出 RGB24 数据
 * @w: 图像宽度
 * @h: 图像高度
 *
 * 使用 BT.601 标准转换公式（整数近似版，避免浮点运算）：
 *   R = Y + 1.402 × (V - 128)
 *   G = Y - 0.344 × (U - 128) - 0.714 × (V - 128)
 *   B = Y + 1.772 × (U - 128)
 *
 * 系数放大 256 倍后：
 *   1.402 → 359, 0.344 → 88, 0.714 → 183, 1.772 → 454
 */
void yuyv_to_rgb(const uint8_t *yuyv, uint8_t *rgb, int w, int h)
{
    for (int i = 0; i < h; i++)
    {
        for (int j = 0; j < w; j += 2)
        {
            int idx = (i * w + j) * 2;
            int y0 = yuyv[idx];           /* 像素 0 的亮度 */
            int u  = yuyv[idx + 1] - 128; /* 色度 U（去中心化） */
            int y1 = yuyv[idx + 2];       /* 像素 1 的亮度 */
            int v  = yuyv[idx + 3] - 128; /* 色度 V（去中心化） */

            /* ---- 像素 0 的 RGB（BT.601 整数近似） ---- */
            int r0 = y0 + (359 * v) / 256;
            int g0 = y0 - (88 * u + 183 * v) / 256;
            int b0 = y0 + (454 * u) / 256;

            /* ---- 像素 1 共用同一组 U/V ---- */
            int r1 = y1 + (359 * v) / 256;
            int g1 = y1 - (88 * u + 183 * v) / 256;
            int b1 = y1 + (454 * u) / 256;

            /* ---- 钳位到 0~255 并写入 ---- */
            int out = (i * w + j) * 3;
            rgb[out]     = r0 < 0 ? 0 : (r0 > 255 ? 255 : r0);
            rgb[out + 1] = g0 < 0 ? 0 : (g0 > 255 ? 255 : g0);
            rgb[out + 2] = b0 < 0 ? 0 : (b0 > 255 ? 255 : b0);
            rgb[out + 3] = r1 < 0 ? 0 : (r1 > 255 ? 255 : r1);
            rgb[out + 4] = g1 < 0 ? 0 : (g1 > 255 ? 255 : g1);
            rgb[out + 5] = b1 < 0 ? 0 : (b1 > 255 ? 255 : b1);
        }
    }
}


/* ===== RGB24 → BMP（调试用图片保存） ===== */

/**
 * rgb_to_bmp() - RGB24 → BMP 文件
 * @rgb: 输入 RGB24 数据
 * @output: 输出 BMP 文件名
 * @w: 图像宽度
 * @h: 图像高度
 *
 * BMP 文件格式要求：
 * 1. 文件头 + 信息头 + 像素数据
 * 2. 像素数据从下到上存储（BMP 标准）
 * 3. 像素排列为 BGR（不是 RGB）
 * 4. 每行 4 字节对齐
 *
 * 用于调试时快速保存一帧图像查看。
 */
void rgb_to_bmp(const uint8_t *rgb, const char *output, int w, int h)
{
    int stride = (w * 3 + 3) & ~3;                 /* 每行 4 字节对齐 */
    int image_size = stride * h;

    /* BMP 文件头（14 字节） */
    typedef struct
    {
        unsigned short type;        /* "BM" = 0x4D42 */
        unsigned int size;          /* 文件总大小 */
        unsigned short reserved1;
        unsigned short reserved2;
        unsigned int offset;        /* 像素数据偏移 */
    } __attribute__((packed)) BmpHeader;

    /* BMP 信息头（40 字节） */
    typedef struct
    {
        unsigned int size;          /* 本结构体大小 40 */
        int width, height;          /* 图像宽高（高度为正：从下到上） */
        unsigned short planes;      /* 颜色平面数，固定为 1 */
        unsigned short bits;        /* 每像素位数，24 */
        unsigned int compression;   /* 压缩方式，0=不压缩 */
        unsigned int image_size;    /* 像素数据大小（含对齐） */
        int x_pels, y_pels;         /* 每米像素数（0 表示不指定） */
        unsigned int clr_used;      /* 调色板颜色数（24 位不用） */
        unsigned int clr_important;
    } __attribute__((packed)) BmpInfo;

    BmpHeader header =
    {
        0x4D42,
        sizeof(BmpHeader) + sizeof(BmpInfo) + image_size,
        0, 0,
        sizeof(BmpHeader) + sizeof(BmpInfo)
    };

    BmpInfo info =
    {
        sizeof(BmpInfo),
        w, h, 1, 24, 0, image_size, 0, 0, 0, 0
    };

    FILE *fp = fopen(output, "wb");
    if (!fp)
    {
        perror("fopen bmp");
        return;
    }

    /* 写入 BMP 头 */
    fwrite(&header, sizeof(header), 1, fp);
    fwrite(&info, sizeof(info), 1, fp);

    /* 写入像素数据：从下到上，BGR 排列 */
    unsigned char *row = malloc(stride);
    for (int y = h - 1; y >= 0; y--)
    {
        memset(row, 0, stride);
        for (int x = 0; x < w; x++)
        {
            row[x * 3]     = rgb[(y * w + x) * 3 + 2];  /* B */
            row[x * 3 + 1] = rgb[(y * w + x) * 3 + 1];  /* G */
            row[x * 3 + 2] = rgb[(y * w + x) * 3];      /* R */
        }
        fwrite(row, 1, stride, fp);
    }

    free(row);
    fclose(fp);
}