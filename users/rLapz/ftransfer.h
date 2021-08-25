// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer header
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#ifndef FTRANSFER_H
#define FTRANSFER_H

#include <stdint.h>

/* macros */
typedef enum {
	FUNC_RECV, FUNC_SEND
} FuncFileHandlerMode;

typedef union {
	ssize_t (*send)(int, const void *, size_t, int);
	ssize_t (*recv)(int, void *, size_t, int);

	ssize_t (*func)(int, void *, size_t, int);
} func_file_hander;

typedef struct __attribute__((packed)) packet_t {
	uint64_t	file_size;
	uint8_t		file_name_len;
	char		file_name[0xffu];
} packet_t;


/* applying the configuration */
#include "config.h"


int set_sigaction(struct sigaction *act, void (*func)(int));
int init_tcp(struct sockaddr_in *sock, const char *addr,
		const uint16_t port);
int file_prop_handler(const int fd, packet_t *prop, FuncFileHandlerMode m);
int file_check(const packet_t *p);
int run_server(int argc, char *argv[]);
int run_client(int argc, char *argv[]);
void print_help(void);

#endif
