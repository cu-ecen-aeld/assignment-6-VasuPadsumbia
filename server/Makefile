# Make the aesdsocket server executable
# Usage: make
# Use the CROSS_COMPILE variable to specify a cross-compiler prefix
# If CROSS_COMPILE is not set, use the default gcc compiler
# If CROSS_COMPILE is set, use the specified cross-compiler
ifndef CROSS_COMPILE
CC = gcc
TARGET = aesdsocket
else
CC = $(CROSS_COMPILE)gcc
TARGET = aesdsocket.elf
endif

CFLAGS ?= -Wall -Wextra -O2 -pthread
LDFLAGS = #-T linker_script.ld
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

