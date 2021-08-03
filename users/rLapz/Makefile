# SPDX-License-Identifier: GPL-2.0

#
# Want to change the compiler?
#   `make CC=clang`
#
CC := cc
LD := $(CC)
CFLAGS := -Os -Wall -Wextra -fpie -fPIE -std=c99 \
	  -pedantic-errors -ggdb3 -fno-omit-frame-pointer
LDFLAGS := -Os -fpie -fPIE
OBJ := ftransfer.o server.o client.o


all: ftransfer

clean:
	rm -vf ftransfer $(OBJ)

server.o: server.c ftransfer.h
	$(CC) $(CFLAGS) -c -o $(@) $(<)

client.o: client.c ftransfer.h
	$(CC) $(CFLAGS) -c -o $(@) $(<)

ftransfer.o: ftransfer.c ftransfer.h
	$(CC) $(CFLAGS) -c -o $(@) $(<)

ftransfer: $(OBJ)
	$(LD) $(LDFLAGS) -o $(@) $(^)

.PHONY: all clean
