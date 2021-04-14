// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer header
 *
 * Copyright (C) 2021  Ammar Faizi <ammarfaizi2@gmail.com>
 */

#ifndef FTRANSFER_H
#define FTRANSFER_H

#include <stdint.h>

void print_help(void);
int run_server(int argc, char *argv[]);
int run_client(int argc, char *argv[]);

typedef struct __attribute__((packed)) packet_t {
	uint64_t	file_size;
	uint8_t		file_name_len;
	char		file_name[0xffu];
} packet_t;



#endif
