CC      = gcc
CFLAGS  = -Wall -Wextra -g
LDFLAGS = -lpthread
TARGET  = server
SRCS    = src/main.c src/camera.c src/stream.c src/server.c src/handler.c src/serial.c src/log.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS) src/*.o