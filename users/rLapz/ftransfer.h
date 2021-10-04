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
	char     file_name[0xffu];
} packet_t;


/* applying the configuration */
#include "config.h"


void die(const char *msg);
void interrupt_handler(int sig);
void set_signal(void);
int file_check(const packet_t *p);
int run_server(int argc, char *argv[]);
int run_client(int argc, char *argv[]);
void print_help(void);

#endif

