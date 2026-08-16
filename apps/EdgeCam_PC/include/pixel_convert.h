/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  pixel_convert.h
 *    Description:  像素格式转换工具（PC 端）
 *                  YUYV → I420（x264 编码输入）
 *                  YUYV → RGB24（预览/OpenCV 用）
 *                  RGB24 → BMP（调试用图片保存）
 *                 
 *        Version:  1.0.0(2026/07/01)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/07/01 10:52:41"
 *                 
 ********************************************************************************/

#ifndef PIXEL_CONVERT_H
#define PIXEL_CONVERT_H

#include <stdint.h>

/* ===== 格式转换 API ===== */

/**
 * yuyv_to_i420() - YUYV 4:2:2 打包格式 → I420 4:2:0 平面格式
 * @yuyv: 输入 YUYV 数据（每 2 个像素 4 字节）
 * @i420: 输出 I420 数据（Y 平面 + U 平面 + V 平面）
 * @w: 图像宽度
 * @h: 图像高度
 *
 * I420 是 x264 编码器的原生输入格式。
 * YUYV 采样：水平 4:2:2（每 2 像素共享 U/V）
 * I420 采样：水平/垂直 4:2:0（每 2×2 像素共享 U/V）
 */
void yuyv_to_i420(const uint8_t *yuyv, uint8_t *i420, int w, int h);

/**
 * yuyv_to_rgb() - YUYV 4:2:2 → RGB24
 * @yuyv: 输入 YUYV 数据
 * @rgb: 输出 RGB24 数据（R-G-B 连续排列）
 * @w: 图像宽度
 * @h: 图像高度
 *
 * 使用 BT.601 标准 YUV→RGB 转换公式（整数近似版）。
 * RGB24 可用于 ffplay 预览或 OpenCV 图像处理。
 */
void yuyv_to_rgb(const uint8_t *yuyv, uint8_t *rgb, int w, int h);

/**
 * rgb_to_bmp() - RGB24 → BMP 文件（用于调试）
 * @rgb: 输入 RGB24 数据
 * @output: 输出 BMP 文件名
 * @w: 图像宽度
 * @h: 图像高度
 *
 * 生成标准的 24 位 BMP 文件，可用图片查看器直接打开。
 * BMP 要求：从下到上存储，BGR 排列，每行 4 字节对齐。
 * 用于调试时快速查看采集或编码前后的图像效果。
 */
void rgb_to_bmp(const uint8_t *rgb, const char *output, int w, int h);

#endif /* PIXEL_CONVERT_H */