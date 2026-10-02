CC      = gcc
CFLAGS  = -Wall -Wextra -g
LDFLAGS = -lpthread
TARGET  = server
SRCS    = src/main.c src/camera.c src/stream.c src/server.c src/handler.c src/serial.c src/log.c
OBJS    = $(SRCS:.c=.o)
DEPS    = $(OBJS:.o=.d)          # 【新增】每个 .o 对应的头文件依赖文件(.d)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<   # 【修改】-MMD -MP：编译时自动生成同名 .d，记录该 .c 用到的 .h

# 【新增】读入自动生成的依赖：以后改 .h，相关 .c 会自动重新编译
-include $(DEPS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS) src/*.o $(DEPS)   # 【修改】clean 时一并删掉自动生成的 .d
