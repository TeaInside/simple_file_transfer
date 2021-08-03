// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer (entry file)
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ftransfer.h"

/* global variables */
static const char *app = NULL;


void
print_help(FILE *f)
{
	if (f == stderr)
		perror(NULL);

	fprintf(f, "Usage: \n");
	fprintf(f, "  %s server [bind_addr] [bind_port]\n", app);
	fprintf(f, "  %s client [server_addr] [server_port] [filename]\n", app);
}

void
print_progress(const char *label, uint64_t i, uint64_t total)
{

	uint8_t per = (i * 100) / total;

	printf("\r%s %lu bytes -> %lu bytes [%u%%]",
			label, i, total, per);

	fflush(stdout);

	if (per == 100)
		printf(" - " WHITE_BOLD_E "Done!" END_E "\n");
}


int
main(int argc, char *argv[])
{
	app = argv[0];

	if (argc < 2) {
		print_help(stdout);
		return 0;
	}

	argc -= 2;
	if (strncmp("server", argv[1], 6) == 0)
		return run_server(argc, argv + 2);

	else if (strncmp("client", argv[1], 6) == 0)
		return run_client(argc, argv + 2);

	errno = EINVAL;
	print_help(stderr);

	return errno;
}
