# /* 
#  * First Written : 2020 05 13
#  * Makefile
#  * Written by    : Aviezab
#  * MIT License
#  * Clang-format
# */

# CC=clang
# ifeq (, $(shell which clang))
# $(error "No clang in $(PATH), using gcc")
# CC=gcc
# endif

CC=gcc

all:
	$(CC) src/client.c -o client.elf -I./include
	$(CC) src/server.c -o server.elf -I./include

clean:
	rm server.elf
	rm client.elf