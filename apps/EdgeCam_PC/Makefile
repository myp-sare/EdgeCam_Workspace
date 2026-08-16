CC      = gcc
CFLAGS  = -Iinclude -I$(EDGERTSP)/include
TARGET  = build/edgecam
SRCS    = src/main.c src/v4l2_capture.c src/pixel_convert.c src/h264_encoder.c
LDFLAGS = -L$(EDGERTSP)/build -lrtpsender -lx264 -lpthread
OBJS    = $(SRCS:src/%=build/obj/%)
OBJS    := $(OBJS:.c=.o)
EDGERTSP ?= ../../libs/EdgeRTSP

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

build/obj/%.o: src/%.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)