# SPDX-License-Identifier: GPL-2.0

#
# Want to change the compiler?
#   `make CC=clang`
#
CC := cc
LD := $(CC)
CFLAGS := -O2 -Wall -Wextra -fpie -fPIE -std=c99 \
	  -ggdb3 -fno-omit-frame-pointer \
	  -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS := -O2 -fpie -fPIE
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

$(OBJ): config.h

.PHONY: all clean
