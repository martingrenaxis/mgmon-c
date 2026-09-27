# MG Monitor - 6809 emulator
# Build with: make
# Clean with: make clean

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic

SRCS    = mgmon.c asm09.c dis09.c sim09.c
TARGET  = mgmon

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
