# ============================================================
# EdgeCam_RV1106 Makefile
# 依赖: EdgeRTSP 库 + MPP 库 + Rockit 库 + RGA 库
# ============================================================

# ===== 交叉编译工具链 =====
TOOLCHAIN_PATH = /home/mayanping/luckfox-dev/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin
CC = $(TOOLCHAIN_PATH)/arm-rockchip830-linux-uclibcgnueabihf-gcc
AR = $(TOOLCHAIN_PATH)/arm-rockchip830-linux-uclibcgnueabihf-ar
STRIP = $(TOOLCHAIN_PATH)/arm-rockchip830-linux-uclibcgnueabihf-strip

# ===== SDK 路径 =====
SDK_PATH ?= /home/mayanping/luckfox-dev/luckfox-pico

# ===== 路径配置 =====
EDGERTSP_DIR = ../../libs/EdgeRTSP

# MPP 头文件路径
MPP_MPI_INC = $(SDK_PATH)/media/mpp/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf/include
MPP_ROCKIT_INC = $(SDK_PATH)/media/rockit/rockit/mpi/sdk/include

# RGA 头文件路径
RGA_INC = $(SDK_PATH)/media/rga/release_rga_rv1106_arm-rockchip830-linux-uclibcgnueabihf/include

# MPP 库路径
MPP_LIB = $(SDK_PATH)/media/mpp/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf/lib
# Rockit 库路径 (RV1106 是 32 位)
ROCKIT_LIB = $(SDK_PATH)/media/rockit/rockit/lib/lib32
# RGA 库路径
RGA_LIB = $(SDK_PATH)/media/rga/release_rga_rv1106_arm-rockchip830-linux-uclibcgnueabihf/lib

# ===== 编译选项 =====
CFLAGS = -Wall -O2 -g
CFLAGS += -I./include
CFLAGS += -I$(EDGERTSP_DIR)/include
CFLAGS += -I$(MPP_MPI_INC)
CFLAGS += -I$(MPP_ROCKIT_INC)
CFLAGS += -I$(RGA_INC)
CFLAGS += -D_GNU_SOURCE
CFLAGS += -fPIC

# ✅ 移除 -lrp，只保留需要的库
LDFLAGS = -lpthread -lm
LDFLAGS += -L$(MPP_LIB) -lrockchip_mpp
LDFLAGS += -L$(ROCKIT_LIB) -lrockit
LDFLAGS += -L$(RGA_LIB) -lrga
LDFLAGS += -L$(EDGERTSP_DIR)/build -lrtpsender

# 添加运行时库路径
LDFLAGS += -Wl,-rpath-link,$(MPP_LIB)
LDFLAGS += -Wl,-rpath-link,$(ROCKIT_LIB)
LDFLAGS += -Wl,-rpath-link,$(RGA_LIB)

# ===== 源文件 =====
SRCS = src/main.c src/v4l2_capture.c src/mpp_encoder.c
OBJS = $(SRCS:src/%.c=build/obj/%.o)
TARGET = build/edgecam_rv1106

# ===== 规则 =====
all: dirs $(TARGET)

dirs:
	@mkdir -p build/obj

$(TARGET): $(OBJS)
	@echo "  LD    $@"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	$(STRIP) $@
	@echo "  STRIP $@"
	@echo "编译完成: $(TARGET)"
	@ls -lh $(TARGET)

build/obj/%.o: src/%.c
	@echo "  CC    $<"
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c -o $@ $<

# ===== 清理 =====
clean:
	rm -rf build/obj build/edgecam_rv1106
	@echo "清理完成"

# ===== 安装到板子 =====
install: $(TARGET)
	@echo "请手动将 $(TARGET) 复制到板子:"
	@echo "  scp $(TARGET) root@<板子IP>:/root/"
	@echo "  # 或使用 nfs 挂载"

# ===== 帮助 =====
help:
	@echo "====================================="
	@echo "EdgeCam_RV1106 Makefile"
	@echo "用法:"
	@echo "  make          - 编译程序"
	@echo "  make clean    - 清理编译文件"
	@echo "  make install  - 显示安装命令"
	@echo ""
	@echo "环境变量:"
	@echo "  SDK_PATH     - SDK路径 (当前: $(SDK_PATH))"
	@echo "====================================="

.PHONY: all clean install help dirs