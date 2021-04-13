// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server
 *
 * Copyright (C) 2021  Your Name <your_email@domain.com>
 */

#include <stdio.h>
#include <errno.h>

#include "ftransfer.h"


int run_server(int argc, char *argv[])
{
	/*
	 * argv[0] is bind address
	 * argv[1] is bind port
	 */

	if (argc != 2) {
		printf("Error: Invalid argument on run_server\n");
		print_help();
		return EINVAL;
	}


	(void)argv;
	/* Your code here... */

	return 0;
}
