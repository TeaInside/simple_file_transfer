// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer (entry file)
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <arpa/inet.h>

#include "ftransfer.h"

static const char *app = NULL;


int
set_sigaction(struct sigaction *act, void (*func)(int))
{
	memset(act, 0, sizeof(struct sigaction));

	act->sa_handler = func;
	if (sigaction(SIGINT, act, NULL) < 0)
		return -1;
	if (sigaction(SIGTERM, act, NULL) < 0)
		return -1;
	if (sigaction(SIGHUP, act, NULL) < 0)
		return -1;

	act->sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, act, NULL) < 0)
		return -1;

	return 0;
}

int
init_tcp(struct sockaddr_in *sock, const char *addr, const uint16_t port)
{
	int sock_d = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock_d < 0)
		goto ret;

	memset(sock, 0, sizeof(struct sockaddr_in));

	/* TCP configuration */
	sock->sin_family      = AF_INET;
	sock->sin_addr.s_addr = inet_addr(addr);
	sock->sin_port        = htons(port);

ret:
	return sock_d;
}

int
file_prop_handler(const int fd, packet_t *prop, FuncFileHandlerMode m)
{
	func_file_hander f;
	char   *raw_prop = (char *)prop;
	ssize_t p_bytes;
	size_t  t_bytes  = 0,
		p_size   = sizeof(packet_t);

	switch (m) {
	case FUNC_SEND:
		f.send = send;
		break;

	case FUNC_RECV:
		f.recv = recv;
		break;

	default:
		errno = EINVAL;
		return -errno;
	}

	while (t_bytes < p_size) {
		p_bytes = f.func(fd, (char *)raw_prop + t_bytes,
					p_size - t_bytes, 0);

		if (p_bytes <= 0)
			break;

		t_bytes += (size_t)p_bytes;
	}

	if (t_bytes != sizeof(packet_t))
		return -errno;

	return 0;
}

int
file_check(const packet_t *p)
{
	if (p->file_name_len == 0 || strstr(p->file_name, "..") != NULL) {
		errno = EINVAL;
		return -errno;
	}

	return 0;
}

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
	if (!strcmp("server", argv[1]))
		return run_server(argc, argv + 2);
	else if (!strcmp("client", argv[1]))
		return run_client(argc, argv + 2);

	printf("Error: Invalid argument \"%s\"\n\n", argv[1]);
	print_help();
	return EINVAL;
}
