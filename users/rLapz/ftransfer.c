// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer (entry file)
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>

#include "ftransfer.h"

/* global variables */
static const char *app = NULL;


int
set_sigaction(struct sigaction *act, void (*func)(int))
{
	memset(act, 0, sizeof(struct sigaction));

	act->sa_handler = func;
	if (sigaction(SIGINT, act, NULL) < 0)
		goto err;
	if (sigaction(SIGTERM, act, NULL) < 0)
		goto err;
	if (sigaction(SIGHUP, act, NULL) < 0)
		goto err;

	act->sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, act, NULL) < 0)
		goto err;

	return 0;

err:
	return -1;
}

int
init_socket(struct sockaddr_in *sock, const char *addr, const uint16_t port)
{
	int socket_d = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_d < 0)
		goto ret;

	memset(sock, 0, sizeof(struct sockaddr_in));

	/* TCP configuration */
	sock->sin_family      = AF_INET;
	sock->sin_addr.s_addr = inet_addr(addr);
	sock->sin_port        = htons(port);

ret:
	return socket_d;
}

void
print_help(FILE *f)
{
	if (f == stderr)
		perror(NULL);

	fprintf(f, "Usage: \n");
	fprintf(f, "  %s server [bind_addr] [bind_port]\n", app);
	fprintf(f, "  %s client [server_addr] [server_port] [filename]\n", app);
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

