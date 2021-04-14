// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer client
 *
 * Copyright (C) 2021  Ammar Faizi <ammarfaizi2@gmail.com>
 */

#include <stdio.h>
#include <errno.h>

#include "ftransfer.h"

#define DEBUG			(1)
#define SEND_BUFFER_SIZE	(0x4000u)


struct client_state {
	int		tcp_fd;
	const char	*target_file;
	FILE		*handle;
};

static int internal_run_client(char *argv[])
{
	int ret;
	struct client_state *state;
}


int run_client(int argc, char *argv[])
{
	/*
	 * argv[0] is server address
	 * argv[1] is server port
	 * argv[2] is filename
	 */

	if (argc != 3) {
		printf("Error: Invalid argument on run_client\n");
		print_help();
		return EINVAL;
	}

	return -internal_run_client(argv);
}
