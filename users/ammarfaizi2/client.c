// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer client
 *
 * Copyright (C) 2021  Ammar Faizi <ammarfaizi2@gmail.com>
 */

#include <stdio.h>
#include <errno.h>

#include "ftransfer.h"


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


	(void)argv;
	/* Your code here... */

	return 0;
}
