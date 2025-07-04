# Cross compilation Makefile for a simple C project

ifndef CROSS_COMPILE
CC = gcc
TARGET = writer
else
CC = $(CROSS_COMPILE)gcc
TARGET = writer.elf
endif
# Use the CROSS_COMPILE variable to specify a cross-compiler prefix
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS = #-T linker_script.ld
SRC = writer.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) *.d *.elf

.PHONY: all clean	
