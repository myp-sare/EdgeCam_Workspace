/*********************************************************************************
 *      Copyright:  (C) 2026 Mayanping<3598023002@qq.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.c
 *    Description:  MPP 硬件编码器实现（RV1106 端）
 *                  输入 NV12 → 输出 H264 裸流（vendor packet ABI）
 *
 *                  ===== RV1106 MPP 编码器关键特性 =====
 *                  1. 输入缓冲区必须物理连续（硬件 DMA 要求）
 *                  2. 编码输出通过 vendor packet 描述符获取
 *                  3. 数据位于内核环形缓冲区，需通过 /dev/mpi/valloc 读取
 *                  4. 与 PC 端 x264 不同：x264 输出 Annex B 格式，MPP 输出需手动提取
 *
 *                  ===== 与 PC 端 x264 的差异 =====
 *                  | PC 端 x264           | RV1106 MPP              |
 *                  |----------------------|-------------------------|
 *                  | 软件编码（CPU）      | 硬件编码（专用电路）    |
 *                  | 输出 Annex B 格式    | 输出 vendor packet       |
 *                  | 标准 NALU + startcode| 需通过 /dev/mpi/valloc 取流 |
 *                  | 零拷贝可选           | 物理连续内存（DMA 必需） |
 *                 
 *        Version:  1.0.0(2026/08/05)
 *         Author:  Mayanping <3598023002@qq.com>
 *      ChangeLog:  1, Release initial version on "2026/08/05 08:52:40"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "rockchip/rk_mpi_mb_cmd.h"
#include "rockchip/rk_venc_cfg.h"
#include "rockchip/rk_venc_rc.h"

#include "mpp_encoder.h"

/* ===== 宏定义 ===== */
#define MPP_ENCODER_BUILD_TAG      "vendor-packet-v9.2-quiet-runtime"
#define VALLOC_DEVICE              "/dev/mpi/valloc"      /* vendor packet 取流设备 */
#define DEFAULT_ENCODER_FPS        30
#define DEFAULT_ENCODER_GOP        30
#define DEFAULT_ENCODER_BIT_RATE   4000000

/* ===== 简单日志宏 ===== */
#define LOG_DEBUG(fmt, ...) printf("[MPP] DEBUG " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  printf("[MPP] INFO " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  printf("[MPP] WARN " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[MPP] ERROR " fmt "\n", ##__VA_ARGS__)


/* ===== 内存计算工具 ===== */

/**
 * align_up_size() - 向上对齐到指定边界
 * @value: 待对齐的值
 * @alignment: 对齐边界（必须为 2 的幂）
 * Return: 对齐后的值
 *
 * 硬件编码器要求 64 字节对齐，这是 DMA 传输的基本要求。
 */
static size_t align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

/**
 * visible_nv12_size() - 计算可见区域 NV12 大小
 * @width: 图像宽度
 * @height: 图像高度
 * Return: NV12 数据大小（不含对齐空洞）
 *
 * NV12 格式：Y 平面 + UV 交错平面
 * 大小 = width × height × 3/2
 */
static size_t visible_nv12_size(int width, int height)
{
    return (size_t)width * (size_t)height * 3U / 2U;
}

/**
 * allocated_nv12_size() - 计算对齐后的 NV12 缓冲区大小
 * @width: 图像宽度
 * @height: 图像高度
 * Return: 64 字节对齐后的缓冲区大小
 *
 * 硬件 DMA 要求 64 字节对齐，因此实际分配的缓冲区可能比 visible 大。
 * 多余部分用于对齐空洞，拷贝时只拷贝 visible 部分。
 */
static size_t allocated_nv12_size(int width, int height)
{
    size_t aligned_width = align_up_size((size_t)width, 64U);
    size_t aligned_height = align_up_size((size_t)height, 64U);
    return aligned_width * aligned_height * 3U / 2U;
}


/* ===== 调试工具 ===== */

/**
 * print_first_bytes() - 打印 H264 数据的前 16 字节
 * @data: 数据指针
 * @length: 数据长度
 *
 * 用于调试，验证编码输出是否以正确的起始码开头。
 * 合法的 H264 流应以 00 00 00 01 或 00 00 01 开头。
 */
static void print_first_bytes(const uint8_t *data, size_t length)
{
    char text[16U * 3U + 1U];
    size_t i;
    size_t offset = 0U;
    size_t show = length < 16U ? length : 16U;

    memset(text, 0, sizeof(text));

    for (i = 0U; i < show && offset < sizeof(text); ++i)
    {
        int written = snprintf(text + offset,
                               sizeof(text) - offset,
                               "%s%02x",
                               i == 0U ? "" : " ",
                               data[i]);
        if (written < 0 || (size_t)written >= sizeof(text) - offset)
        {
            break;
        }
        offset += (size_t)written;
    }

    printf("[MPP] H264 first bytes: %s\n", text);
}


/* ===== 编码器配置 ===== */

/**
 * set_encoder_config() - 配置 MPP 编码器参数
 * @encoder: 编码器上下文
 * Return: 0 成功，-1 失败
 *
 * 配置项：
 *   - 编码器类型: H264 (AVC)
 *   - 输入格式: NV12 (MPP_FMT_YUV420SP)
 *   - 码率模式: CBR（恒定码率）
 *   - GOP: 每 N 帧一个 IDR
 *   - 帧率: fps_in_num/fps_out_num
 */
static int set_encoder_config(MppEncoder *encoder)
{
    MPP_RET ret;
    MppEncCfg cfg = NULL;

    /* 1. 初始化配置结构体 */
    ret = mpp_enc_cfg_init(&cfg);
    if (ret != MPP_OK || cfg == NULL)
    {
        LOG_ERROR("mpp_enc_cfg_init failed ret=%d", ret);
        return -1;
    }

    /* 2. 获取当前配置 */
    ret = encoder->mpi->control(encoder->ctx, MPP_ENC_GET_CFG, cfg);
    if (ret != MPP_OK)
    {
        LOG_ERROR("MPP_ENC_GET_CFG failed ret=%d", ret);
        mpp_enc_cfg_deinit(cfg);
        return -1;
    }

    /* 3. 设置编码参数 */
    mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingAVC);

    /* ---- 输入格式 ---- */
    mpp_enc_cfg_set_s32(cfg, "prep:width", encoder->width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", encoder->height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", encoder->width);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", encoder->height);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);  /* NV12 */

    /* ---- 码率控制 ---- */
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);    /* 恒定码率 */
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", encoder->bit_rate);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", encoder->bit_rate_min);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", encoder->bit_rate_max);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", encoder->gop);

    /* ---- 帧率 ---- */
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", encoder->fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", encoder->fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);

    /* 4. 应用配置 */
    ret = encoder->mpi->control(encoder->ctx, MPP_ENC_SET_CFG, cfg);
    mpp_enc_cfg_deinit(cfg);

    if (ret != MPP_OK)
    {
        LOG_ERROR("MPP_ENC_SET_CFG failed ret=%d", ret);
        return -1;
    }

    return 0;
}


/* ===== Vendor Packet 读取（RV1106 特有） ===== */

/**
 * copy_vendor_packet() - 从 MPP 环形缓冲区拷贝 H264 数据
 * @packet: venc_packet 描述符
 * @out: 输出参数，指向 H264 数据的指针
 * Return: 数据长度，-1 失败
 *
 * ===== RV1106 特有机制：vendor packet 取流 =====
 *
 * MPP 编码器的输出不是直接可读的用户空间内存，而是位于内核的环形缓冲区。
 * 取流步骤：
 *   1. open("/dev/mpi/valloc") 获取设备句柄
 *   2. ioctl(VALLOC_IOCTL_MB_GET_FD) 根据 mpi_buf_id 获取 dma-buf fd
 *   3. mmap(dma_buf_fd) 映射内核环形缓冲区到用户空间
 *   4. 根据 offset 和 len 从环形缓冲区拷贝数据（处理 wrap-around）
 *   5. munmap() + close() 释放资源
 *
 * ===== 环形缓冲区机制 =====
 * 如果 offset + len > buf_size，数据会从 offset 读到末尾，
 * 再从开头读剩余部分（环形缓冲区特性）。
 */
static int copy_vendor_packet(const struct venc_packet *packet, uint8_t **out)
{
    struct valloc_mb mb;
    uint8_t *mapped = MAP_FAILED;
    uint8_t *result = NULL;
    size_t packet_len;
    size_t buffer_size;
    size_t offset;
    size_t first_part;
    int valloc_fd = -1;
    int dma_fd = -1;
    int ret = -1;

    if (packet == NULL || out == NULL)
    {
        return -1;
    }

    *out = NULL;

    packet_len = (size_t)packet->len;
    buffer_size = (size_t)packet->buf_size;
    offset = (size_t)packet->offset;

    /* 打印 vendor packet 信息（便于调试） */
    printf("[MPP] vendor packet: mpi_buf_id=%" PRIu32
           " len=%" PRIu32
           " buf_size=%" PRIu32
           " offset=%" PRIu32
           " data_num=%" PRIu32
           " flag=0x%08" PRIx32
           " pts=%" PRIu64 "\n",
           (RK_U32)packet->u64priv_data,
           packet->len,
           packet->buf_size,
           packet->offset,
           packet->data_num,
           packet->flag,
           (uint64_t)packet->u64pts);

    /* 校验参数 */
    if (packet_len == 0U || buffer_size == 0U)
    {
        LOG_ERROR("vendor packet is empty");
        return -1;
    }

    /* offset == buf_size 是环形缓冲区的有效哨兵，表示数据从开头开始 */
    if (offset > buffer_size || packet_len > buffer_size)
    {
        LOG_ERROR("invalid vendor packet range: offset=%zu len=%zu buf_size=%zu",
               offset, packet_len, buffer_size);
        return -1;
    }

    if (offset == buffer_size)
    {
        printf("[MPP] vendor ring boundary: normalize offset=%zu to 0\n", offset);
        offset = 0U;
    }

    /* 1. 打开 valloc 设备 */
    valloc_fd = open(VALLOC_DEVICE, O_RDWR);
    if (valloc_fd < 0)
    {
        LOG_ERROR("open %s failed: %s", VALLOC_DEVICE, strerror(errno));
        return -1;
    }

    /* 2. 根据 mpi_buf_id 获取 dma-buf fd */
    memset(&mb, 0, sizeof(mb));
    mb.mpi_buf_id = (int)(RK_U32)packet->u64priv_data;
    mb.struct_size = (int)sizeof(mb);

    if (ioctl(valloc_fd, VALLOC_IOCTL_MB_GET_FD, &mb) < 0)
    {
        LOG_ERROR("VALLOC_IOCTL_MB_GET_FD failed: mpi_buf_id=%d error=%s",
               mb.mpi_buf_id, strerror(errno));
        goto CLEANUP;
    }

    dma_fd = mb.dma_buf_fd;
    if (dma_fd < 0)
    {
        LOG_ERROR("VALLOC_IOCTL_MB_GET_FD returned invalid dma fd=%d", dma_fd);
        goto CLEANUP;
    }

    printf("[MPP] valloc packet buffer: mpi_buf_id=%d dma_buf_fd=%d size=%d\n",
           mb.mpi_buf_id, dma_fd, mb.size);

    /* 3. mmap 映射环形缓冲区到用户空间 */
    mapped = (uint8_t *)mmap(NULL, buffer_size, PROT_READ, MAP_SHARED, dma_fd, 0);
    if (mapped == MAP_FAILED)
    {
        LOG_ERROR("mmap encoded stream failed: %s", strerror(errno));
        goto CLEANUP;
    }

    /* 4. 分配结果缓冲区并拷贝数据 */
    result = (uint8_t *)malloc(packet_len);
    if (result == NULL)
    {
        LOG_ERROR("malloc H264 output failed, size=%zu", packet_len);
        goto CLEANUP;
    }

    /* 处理环形缓冲区 wrap-around */
    first_part = buffer_size - offset;
    if (first_part > packet_len)
    {
        first_part = packet_len;
    }

    memcpy(result, mapped + offset, first_part);

    if (packet_len > first_part)
    {
        memcpy(result + first_part, mapped, packet_len - first_part);
    }

    /* 打印前 16 字节用于调试 */
    print_first_bytes(result, packet_len);

    *out = result;
    result = NULL;
    ret = (int)packet_len;

CLEANUP:
    free(result);

    if (mapped != MAP_FAILED)
    {
        munmap(mapped, buffer_size);
    }
    if (dma_fd >= 0)
    {
        close(dma_fd);
    }
    if (valloc_fd >= 0)
    {
        close(valloc_fd);
    }

    return ret;
}


/* ===== 编码器生命周期 ===== */

/**
 * mpp_encoder_init() - 使用默认参数初始化 MPP 编码器
 * @encoder: 编码器上下文
 * @width: 图像宽度
 * @height: 图像高度
 * Return: 0 成功，-1 失败
 */
int mpp_encoder_init(MppEncoder *encoder, int width, int height)
{
    return mpp_encoder_init_ex(encoder, width, height,
                               DEFAULT_ENCODER_FPS,
                               DEFAULT_ENCODER_GOP,
                               DEFAULT_ENCODER_BIT_RATE);
}

/**
 * mpp_encoder_init_ex() - 使用自定义参数初始化 MPP 编码器
 * @encoder: 编码器上下文
 * @width: 图像宽度
 * @height: 图像高度
 * @fps: 帧率
 * @gop: GOP 大小
 * @bit_rate: 目标码率 (bps)
 * Return: 0 成功，-1 失败
 *
 * ===== 初始化流程 =====
 * 1. 分配输入缓冲区（MppBuffer，物理连续）
 * 2. 分配辅助缓冲区
 * 3. mpp_create() 创建上下文
 * 4. mpp_init_ext() 初始化编码器（硬件初始化）
 * 5. MPP_SET_OUTPUT_TIMEOUT 设置超时
 * 6. set_encoder_config() 配置编码参数
 */
int mpp_encoder_init_ex(MppEncoder *encoder,
                        int width,
                        int height,
                        int fps,
                        int gop,
                        int bit_rate)
{
    MPP_RET ret;
    MppPollType timeout = MPP_POLL_BLOCK;
    vcodec_attr attr;
    size_t frame_buffer_size;
    int64_t bit_rate_min;
    int64_t bit_rate_max;

    /* 参数校验 */
    if (encoder == NULL ||
        width <= 0 ||
        height <= 0 ||
        fps <= 0 ||
        gop <= 0 ||
        bit_rate <= 0)
    {
        LOG_ERROR("invalid encoder parameter: width=%d height=%d fps=%d gop=%d bitrate=%d",
               width, height, fps, gop, bit_rate);
        return -1;
    }

    /* 计算码率范围（±25%） */
    bit_rate_min = (int64_t)bit_rate * 3LL / 4LL;
    bit_rate_max = (int64_t)bit_rate * 9LL / 8LL;

    if (bit_rate_min <= 0 || bit_rate_max > INT_MAX)
    {
        LOG_ERROR("encoder bitrate range overflow: target=%d", bit_rate);
        return -1;
    }

    /* 初始化结构体 */
    memset(encoder, 0, sizeof(*encoder));

    encoder->width = width;
    encoder->height = height;
    encoder->fps = fps;
    encoder->gop = gop;
    encoder->bit_rate = bit_rate;
    encoder->bit_rate_min = (int)bit_rate_min;
    encoder->bit_rate_max = (int)bit_rate_max;
    encoder->frame_size = visible_nv12_size(width, height);
    encoder->frame_index = 0;

    frame_buffer_size = allocated_nv12_size(width, height);

    LOG_INFO("build: %s", MPP_ENCODER_BUILD_TAG);
    LOG_INFO("NV12 buffers: visible=%zu allocated=%zu",
             encoder->frame_size, frame_buffer_size);
    LOG_INFO("config: fps=%d gop=%d bps_target=%d bps_min=%d bps_max=%d",
             encoder->fps, encoder->gop,
             encoder->bit_rate, encoder->bit_rate_min, encoder->bit_rate_max);

    /* ===== 1. 分配输入缓冲区（物理连续） ===== */
    encoder->group = NULL;  /* 使用默认分配器 */
    ret = mpp_buffer_get(NULL, &encoder->frm_buf, frame_buffer_size);
    printf("[MPP] pre-create frm_buf ret=%d buf=%p\n", ret, encoder->frm_buf);

    if (ret != MPP_OK || encoder->frm_buf == NULL)
    {
        LOG_ERROR("mpp_buffer_get frm_buf failed ret=%d", ret);
        goto FAIL;
    }

    /* ===== 2. 分配辅助缓冲区 ===== */
    ret = mpp_buffer_get(NULL, &encoder->pkt_buf, frame_buffer_size);
    printf("[MPP] pre-create auxiliary buf ret=%d buf=%p\n", ret, encoder->pkt_buf);

    if (ret != MPP_OK || encoder->pkt_buf == NULL)
    {
        LOG_ERROR("mpp_buffer_get auxiliary buffer failed ret=%d", ret);
        goto FAIL;
    }

    /* ===== 3. 创建 MPP 上下文 ===== */
    ret = mpp_create(&encoder->ctx, &encoder->mpi);
    printf("[MPP] mpp_create: ret=%d ctx=%p mpi=%p\n", ret, encoder->ctx, encoder->mpi);

    if (ret != MPP_OK || encoder->ctx == NULL || encoder->mpi == NULL)
    {
        LOG_ERROR("mpp_create failed ret=%d", ret);
        goto FAIL;
    }

    /* ===== 4. 初始化编码器硬件 ===== */
    memset(&attr, 0, sizeof(attr));
    attr.type = MPP_CTX_ENC;
    attr.coding = MPP_VIDEO_CodingAVC;
    attr.chan_id = 0;

    ret = mpp_init_ext(encoder->ctx, &attr);
    printf("[MPP] mpp_init_ext: ret=%d\n", ret);

    if (ret != MPP_OK)
    {
        LOG_ERROR("mpp_init_ext failed ret=%d", ret);
        goto FAIL;
    }

    encoder->initialized = 1;

    /* ===== 5. 设置输出超时 ===== */
    ret = encoder->mpi->control(encoder->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    printf("[MPP] MPP_SET_OUTPUT_TIMEOUT ret=%d\n", ret);

    if (ret != MPP_OK)
    {
        LOG_ERROR("MPP_SET_OUTPUT_TIMEOUT failed ret=%d", ret);
        goto FAIL;
    }

    /* ===== 6. 配置编码参数 ===== */
    if (set_encoder_config(encoder) != 0)
    {
        goto FAIL;
    }

    encoder->header_data = NULL;
    encoder->header_len = 0;
    encoder->header_pending = 0;

    LOG_INFO("MPP encoder initialized");
    return 0;

FAIL:
    mpp_encoder_close(encoder);
    return -1;
}

/**
 * mpp_encoder_encode() - 编码一帧 NV12 图像
 * @encoder: 编码器上下文
 * @nv12: NV12 数据指针
 * @size: 输入数据大小
 * @out: 输出参数，指向 H264 数据的指针
 * Return: 成功返回数据长度，-1 失败，-2 无输出
 *
 * ===== 编码流程 =====
 * 1. 拷贝 NV12 数据到 MppBuffer（硬件 DMA 可直接读取）
 * 2. 创建 MppFrame，设置分辨率/格式/时间戳
 * 3. encode_put_frame() 送入编码器（异步，立即返回）
 * 4. encode_get_packet() 获取编码输出（可能返回 MPP_ERR_AGAIN）
 * 5. copy_vendor_packet() 从环形缓冲区拷贝 H264 数据
 * 6. encode_release_packet() 释放环形缓冲区资源
 *
 * ===== 异步机制 =====
 * encode_put_frame() 只是把帧放入队列，硬件在后台编码。
 * 因此 encode_get_packet() 可能返回 MPP_ERR_AGAIN（无输出）。
 * 调用者需要循环调用直到获取到数据。
 */
int mpp_encoder_encode(MppEncoder *encoder,
                       const uint8_t *nv12,
                       int size,
                       uint8_t **out)
{
    MPP_RET ret;
    MppFrame frame = NULL;
    struct venc_packet vendor_packet;
    MppPacket packet_handle;
    void *frame_ptr;
    size_t frame_buffer_size;
    int result = -1;
    int packet_acquired = 0;

    /* 参数校验 */
    if (encoder == NULL ||
        encoder->ctx == NULL ||
        encoder->mpi == NULL ||
        encoder->frm_buf == NULL ||
        nv12 == NULL ||
        out == NULL)
    {
        LOG_ERROR("mpp_encoder_encode invalid parameter");
        return -1;
    }

    *out = NULL;

    if (size < 0 || (size_t)size < encoder->frame_size)
    {
        LOG_ERROR("NV12 size too small: input=%d expected=%zu",
               size, encoder->frame_size);
        return -1;
    }

    frame_buffer_size = allocated_nv12_size(encoder->width, encoder->height);

    /* ===== 1. 拷贝 NV12 到 MppBuffer ===== */
    frame_ptr = mpp_buffer_get_ptr(encoder->frm_buf);
    if (frame_ptr == NULL)
    {
        LOG_ERROR("mpp_buffer_get_ptr(frm_buf) failed");
        return -1;
    }

    memset(frame_ptr, 0, frame_buffer_size);
    memcpy(frame_ptr, nv12, encoder->frame_size);

    /* ===== 2. 创建 MppFrame ===== */
    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK || frame == NULL)
    {
        LOG_ERROR("mpp_frame_init failed ret=%d", ret);
        goto CLEANUP;
    }

    mpp_frame_set_width(frame, encoder->width);
    mpp_frame_set_height(frame, encoder->height);
    mpp_frame_set_hor_stride(frame, encoder->width);
    mpp_frame_set_ver_stride(frame, encoder->height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_pts(frame, encoder->frame_index++);
    mpp_frame_set_eos(frame, 0);
    mpp_frame_set_buffer(frame, encoder->frm_buf);

    printf("[MPP] start encode frame=%" PRId64 "\n", encoder->frame_index - 1);

    /* ===== 3. 送入编码器（异步） ===== */
    ret = encoder->mpi->encode_put_frame(encoder->ctx, frame);
    printf("[MPP] encode_put_frame ret=%d\n", ret);

    mpp_frame_deinit(&frame);
    frame = NULL;

    if (ret != MPP_OK)
    {
        LOG_ERROR("encode_put_frame failed ret=%d", ret);
        goto CLEANUP;
    }

    /* ===== 4. 获取编码输出 ===== */
    memset(&vendor_packet, 0, sizeof(vendor_packet));
    packet_handle = (MppPacket)&vendor_packet;

    ret = encoder->mpi->encode_get_packet(encoder->ctx, &packet_handle);
    printf("[MPP] encode_get_packet ret=%d handle=%p expected=%p\n",
           ret, packet_handle, (void *)&vendor_packet);

    if (ret != MPP_OK)
    {
        LOG_ERROR("encode_get_packet failed ret=%d", ret);
        goto CLEANUP;
    }

    packet_acquired = 1;

    if (packet_handle != (MppPacket)&vendor_packet)
    {
        LOG_ERROR("unexpected vendor packet handle replacement: %p", packet_handle);
        goto RELEASE_PACKET;
    }

    /* ===== 5. 从环形缓冲区拷贝 H264 数据 ===== */
    result = copy_vendor_packet(&vendor_packet, out);

RELEASE_PACKET:
    /* ===== 6. 释放环形缓冲区资源 ===== */
    if (packet_acquired)
    {
        if (encoder->mpi->encode_release_packet == NULL)
        {
            LOG_ERROR("encode_release_packet API is NULL");
            result = -1;
        }
        else
        {
            ret = encoder->mpi->encode_release_packet(encoder->ctx, &packet_handle);
            printf("[MPP] encode_release_packet ret=%d\n", ret);
            if (ret != MPP_OK)
            {
                free(*out);
                *out = NULL;
                result = -1;
            }
        }
    }

CLEANUP:
    if (frame != NULL)
    {
        mpp_frame_deinit(&frame);
    }

    return result;
}

/**
 * mpp_encoder_close() - 关闭编码器，释放所有资源
 * @encoder: 编码器上下文
 *
 * 清理顺序：
 *   1. 释放头信息缓存
 *   2. mpi->reset() 重置硬件
 *   3. mpp_destroy() 销毁上下文
 *   4. mpp_buffer_put() 释放缓冲区
 *   5. 清空上下文结构体
 */
void mpp_encoder_close(MppEncoder *encoder)
{
    if (encoder == NULL)
    {
        return;
    }

    /* 释放头信息 */
    free(encoder->header_data);
    encoder->header_data = NULL;
    encoder->header_len = 0;
    encoder->header_pending = 0;

    /* 销毁 MPP 上下文 */
    if (encoder->ctx != NULL)
    {
        if (encoder->mpi != NULL && encoder->initialized)
        {
            encoder->mpi->reset(encoder->ctx);
        }

        mpp_destroy(encoder->ctx);
        encoder->ctx = NULL;
    }

    encoder->mpi = NULL;
    encoder->initialized = 0;

    /* 释放缓冲区 */
    if (encoder->frm_buf != NULL)
    {
        mpp_buffer_put(encoder->frm_buf);
        encoder->frm_buf = NULL;
    }

    if (encoder->pkt_buf != NULL)
    {
        mpp_buffer_put(encoder->pkt_buf);
        encoder->pkt_buf = NULL;
    }

    /* 清空结构体 */
    encoder->group = NULL;

    encoder->width = 0;
    encoder->height = 0;
    encoder->fps = 0;
    encoder->gop = 0;
    encoder->bit_rate = 0;
    encoder->bit_rate_min = 0;
    encoder->bit_rate_max = 0;
    encoder->frame_size = 0U;
    encoder->frame_index = 0;

    printf("[MPP] encoder closed\n");
}