// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer header
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#ifndef FTRANSFER_H
#define FTRANSFER_H

#include <stdint.h>
#include "config.h"


#define INFO_MSG     BOLD_YELLOW("[INFO]")
#define ERR_MSG      BOLD_RED("[ERROR]")

#define PERROR(X)    perror(ERR_MSG " " X)
#define FPERROR(...) fprintf(stderr, ERR_MSG " " __VA_ARGS__)
#define INFO(...)    printf(INFO_MSG " "__VA_ARGS__)



typedef struct __attribute__((packed)) packet_t {
	uint64_t file_size               ;
	uint8_t  file_name_len           ;
	char     file_name[FILE_NAME_LEN];
} packet_t;


union pkt_uni {
	packet_t prop            ;
	char     raw[BUFFER_SIZE];    /* see: config.h */
};


void interrupt_handler(int sig)               ;
void set_signal       (void)                  ;
int  file_check       (const packet_t *p)     ;
int  run_server       (int argc, char *argv[]);
int  run_client       (int argc, char *argv[]);
void print_help       (void)                  ;

#endif

