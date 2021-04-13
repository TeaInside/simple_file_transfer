// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer header
 *
 * Copyright (C) 2021  Your Name <your_email@domain.com>
 */

#ifndef FTRANSFER_H
#define FTRANSFER_H

#include <stdint.h>

void print_help(void);
int run_server(int argc, char *argv[]);
int run_client(int argc, char *argv[]);

typedef struct packet_t {
	uint8_t		filename_len;
	char		filename[0xffu];
	uint64_t	file_size;
} packet_t;

#endif
