// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer client
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 *
 * NOTE: true = 1, false = 0
 */

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "ftransfer.h"
#include "util.h"

/* function declarations */
static void  interupt_handler (int sig);
static void  get_file_prop    (packet_t *pkt, char *argv[]);
static FILE *open_file        (const char *file_name);
static int   init_socket      (const char *addr, uint16_t port);
static int   send_packet      (const int client_d, FILE *file, const packet_t *prop);
int          run_client       (int argc, char *argv[]);

/* global variable */
static volatile int interrupted = 0;


/* function implementations */
static void
interupt_handler(int sig)
{
	interrupted = 1;
	(void)sig;
}

static void
get_file_prop(packet_t *pkt, char *argv[])
{
	char	*base_name;
	struct	 stat s;
	size_t	 f_len;

	if (stat(argv[2], &s) < 0)
		die("\"%s\" :", argv[2]);

	base_name = basename(argv[2]);
	f_len	  = strlen(base_name);

	pkt->file_size     = s.st_size;
	pkt->file_name_len = f_len;

	memcpy(pkt->file_name, base_name, f_len);
	pkt->file_name[f_len] = '\0';
}

static FILE *
open_file(const char *file_name)
{
	FILE *f = fopen(file_name, "r");
	if (f == NULL)
		die("open_file():");

	return f;
}

static int
init_socket(const char *addr, uint16_t port)
{
	int socket_d = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_d < 0)
		goto err1;

	struct sockaddr_in srv = {
		.sin_family	 = AF_INET,
		.sin_addr.s_addr = inet_addr(addr),
		.sin_port	 = htons(port),
		.sin_zero	 = {0}
	};

	printf("\nConnecting...");

	if (connect(socket_d, (struct sockaddr *)&srv, sizeof(srv)) < 0)
		goto err0;

	fflush(stdout);
	puts("\rConnected to the server!\n");

	return socket_d;

err0:
	close(socket_d);
err1:
	perror("init_socket()");
	return -errno;
}

static int
send_packet(const int client_d, FILE *file, const packet_t *prop)
{
	ssize_t  send_bytes;
	size_t	 read_bytes;
	uint64_t bytes_sent = 0;
	char	 content[BUFFER_SIZE] = {0};

	/* send file properties */
	send_bytes = send(client_d, prop, sizeof(packet_t), 0);
	if (send_bytes < 0)
		goto err;

	/* send the packet */
	while (bytes_sent < (prop->file_size) && interrupted != 1) {
		read_bytes = fread(content, 1, BUFFER_SIZE, file);
		send_bytes = send(client_d, content, read_bytes, 0);
		if (send_bytes < 0)
			break;

		bytes_sent += (uint64_t)send_bytes;

		print_progress("Sending...", bytes_sent, prop->file_size);
		sleep(1);
	}
	putchar('\n');

	if (errno != 0)
		goto err;

	return 0;

err:
	return -errno;
}

int
run_client(int argc, char *argv[])
{
	/*
	 * argv[0] is the server address
	 * argv[1] is the server port
	 * argv[2] is the file name
	 */

	if (argc != 3) {
		errno = EINVAL;
		print_help(stderr);
		return -errno;
	}

/* ---------------------------------------------- */

	packet_t	 pkt;
	FILE		*file;
	int		 socket_d, s_packet;
	const char	*full_path = argv[2];

	signal(SIGINT,	interupt_handler);
	signal(SIGTERM,	interupt_handler);
	signal(SIGHUP,	interupt_handler);
	signal(SIGPIPE,	SIG_IGN		);

	memset(&pkt, 0, sizeof(packet_t));

	get_file_prop(&pkt, argv);

	puts(WHITE_BOLD_E "File info" END_E);
	printf("|-> Full path   : %s (%zu)\n",	full_path,	strlen(full_path));
	printf("|-> File name   : %s (%u)\n",	pkt.file_name,	pkt.file_name_len);
	printf("|-> File size   : %zu bytes\n", pkt.file_size			 );
	printf("`-> Destination : %s:%s\n",	argv[0],	argv[1]		 );

	file	 = open_file(full_path);
	socket_d = init_socket(argv[0], (uint16_t)atoi(argv[1]));
	if (socket_d < 0)
		goto cleanup1;

	s_packet = send_packet(socket_d, file, &pkt);
	if (s_packet < 0)
		goto cleanup0;

cleanup0:
	close(socket_d);

cleanup1:
	fclose(file);

	if (errno != 0)
		goto err;

	puts("uWu :3");

	return 0;

err:
	fprintf(stderr, "Failed! : %s\n", strerror(errno));
	return -errno;
}
