# ============================================================
# 平台选择：PC 或 RV1106
# 用法：
#   make              # 默认编译 PC 版本
#   make PLATFORM=PC  # 编译 PC 版本
#   make PLATFORM=RV1106  # 编译 RV1106 版本
# ============================================================
PLATFORM ?= PC

# ============================================================
# PC 平台配置
# ============================================================
ifeq ($(PLATFORM), PC)
CC      = gcc
CFLAGS  = -Iinclude
TARGET  = build/rtsp_server
SRCS    = src/main.c src/rtsp_server.c src/utils.c src/rtp_sender.c
LIB     = build/librtpsender.a
LIB_OBJS = build/obj/rtp_sender.o build/obj/rtsp_server.o build/obj/utils.o
endif

# ============================================================
# RV1106 平台配置
# ============================================================
ifeq ($(PLATFORM), RV1106)
# SDK 路径（如果没有通过命令行传入，使用默认值）
SDK_PATH ?= /home/mayanping/luckfox-dev/luckfox-pico

TOOLCHAIN_PATH = $(SDK_PATH)/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin
CC      = $(TOOLCHAIN_PATH)/arm-rockchip830-linux-uclibcgnueabihf-gcc
# 添加 MPP 头文件路径（注意：这里用 rockchip 子目录）
CFLAGS  = -Iinclude -I$(SDK_PATH)/media/mpp/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf/include/rockchip -fPIC
TARGET  = build/rtsp_server
SRCS    = src/main.c src/rtsp_server.c src/utils.c src/rtp_sender.c
LIB     = build/librtpsender.a
LIB_OBJS = build/obj/rtp_sender.o build/obj/rtsp_server.o build/obj/utils.o
endif

# ============================================================
# 通用配置
# ============================================================
OBJS    = $(SRCS:src/%=build/obj/%)
OBJS    := $(OBJS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CC) -o $@ $(OBJS) $(CFLAGS)

# =========== 封库 ==========
lib: $(LIB)

$(LIB): $(LIB_OBJS)
		ar rcs $@ $^

build/obj/%.o: src/%.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $< -o $@
#==========================

run: $(TARGET)
		./$(TARGET)

clean:
		rm -f $(TARGET) $(LIB) $(OBJS)

help:
	@echo "====================================="
	@echo "用法:"
	@echo "  make PLATFORM=PC       # 编译 PC 版本"
	@echo "  make PLATFORM=RV1106   # 编译 RV1106 版本"
	@echo "  make lib PLATFORM=RV1106 # 只编译库"
	@echo "  make clean             # 清理"
	@echo "====================================="