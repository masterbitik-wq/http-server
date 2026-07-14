CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2
LDFLAGS = -lws2_32
TARGET = http_server

all: $(TARGET)

$(TARGET): server.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

server.o: server.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean