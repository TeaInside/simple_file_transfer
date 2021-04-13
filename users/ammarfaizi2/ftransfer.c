// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer (entry file)
 *
 * Copyright (C) 2021  Ammar Faizi <ammarfaizi2@gmail.com>
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "ftransfer.h"

static const char *app = NULL;

void print_help(void)
{
	printf("Usage: \n");
	printf("  %s server [bind_addr] [bind_port]\n", app);
	printf("  %s client [server_addr] [server_port] [filename]\n", app);
}


int main(int argc, char *argv[])
{
	app = argv[0];

	if (argc < 2) {
		print_help();
		return 0;
	}

	argc -= 2;
	if (!strncmp("server", argv[1], 6))
		return run_server(argc, argv + 2);
	else if (!strncmp("client", argv[1], 6))
		return run_client(argc, argv + 2);

	printf("Error: Invalid argument \"%s\"\n\n", argv[1]);
	print_help();
	return EINVAL;
}
