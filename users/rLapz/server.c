// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "ftransfer.h"
#include "util.h"

/* function declarations */
static int init_socket();
int run_server(int argc, char *argv[]);

/* function implementations */
int
run_server(int argc, char *argv[])
{
	/*
	 * argv[0] is the bind address
	 * argv[1] is the bind port
	 */

	if (argc != 2) {
		print_help(stderr);
		return EINVAL;
	}


	(void)argv;
	/* Your code here... */

	return 0;
}
