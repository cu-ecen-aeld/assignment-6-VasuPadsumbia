# Makefile for aesdsocket server
ifndef CROSS_COMPILE
CC ?= gcc
TARGET = aesdsocket
else
CC ?= $(CROSS_COMPILE)gcc
TARGET = aesdsocket.elf
endif

CFLAGS ?= -Wall -Wextra -O2 -pthread
LDFLAGS ?= -pthread

SRC = aesdsocket.c socket.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) *.d *.elf

.PHONY: all clean
