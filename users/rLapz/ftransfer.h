// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer header
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#ifndef FTRANSFER_H
#define FTRANSFER_H

#include <stdint.h>


typedef struct __attribute__((packed)) packet_t {
	uint64_t file_size;
	uint8_t  file_name_len;
	char     file_name[255];
} packet_t;


void print_help(FILE *f);
int  init_socket(struct sockaddr_in *sock, const char *addr,
			const uint16_t port);
int  set_sigaction(struct sigaction *act, void (*func)(int));
int  run_client(int argc, char *argv[]);
int  run_server(int argc, char *argv[]);

#endif
