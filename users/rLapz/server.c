// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include "ftransfer.h"

/* function declarations */
static void  interrupt_handler (int sig);
static int   file_verif        (const char *filename);
static int   init_server       (const char *addr, const uint16_t port);
static FILE *open_file         (const char *file_name);
static void  recv_packet       (int socket_d, packet_t *prop);
int          run_server        (int argc, char *argv[]);

/* global variables */
static volatile int interrupted = 0;

/* function implementations */
static void
interrupt_handler(int sig)
{
	interrupted  = 1;
	errno        = EINTR;
	(void)sig;
}

static FILE *
open_file(const char *file_name)
{
	FILE *f = fopen(file_name, "w");
	if (f == NULL)
		perror(file_name);

	return f;
}

static int
file_verif(const char *filename)
{
	if (strstr(filename, "..") != NULL) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

static int
init_server(const char *addr, const uint16_t port)
{
	int sock_opt = 1;
	int socket_d;
	struct sockaddr_in srv;

	socket_d = init_socket(&srv, addr, port);
	if (socket_d < 0) {
		perror("socket");
		goto err1;
	}

	/* socket option
	 * reuse IP address */
	if (setsockopt(socket_d, SOL_SOCKET, SO_REUSEADDR,
				&sock_opt, sizeof(sock_opt)) < 0) {
		perror("setsockopt");
		goto err0;
	}

	if (bind(socket_d, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
		perror("bind");
		goto err0;
	}

	return socket_d;

err0:
	close(socket_d);

err1:
	return -1;
}

static void
recv_packet(int socket_d, packet_t *prop)
{
	FILE     *file;
	int       client_d;
	ssize_t   recv_bytes;
	size_t    writen_bytes;
	struct    sockaddr_in client;
	char      content[BUFFER_SIZE];
	uint64_t  total_bytes	= 0;
	socklen_t client_len	= sizeof(struct sockaddr_in);
	
	if (listen(socket_d, 3) < 0) {
		perror("listening");
		return;
	}

	client_d = accept(socket_d, (struct sockaddr *)&client, &client_len);
	if (client_d < 0) {
		perror("accept");
		return;
	}

	/* get file properties from client */
	puts("Receiving file properties...");
	recv_bytes = recv(client_d, prop, sizeof(packet_t), 0);
	if (recv_bytes < 0) {
		perror("recv");
		goto cleanup1;
	}

	if (file_verif(prop->file_name) < 0) {
		fputs("Invalid file name! :p\n\n\n", stderr);
		goto cleanup1;
	}

	puts(WHITE_BOLD_E "File info" END_E);
	printf("|-> File name   : %s (%u)\n",	prop->file_name, prop->file_name_len);
	printf("`-> File size   : %lu bytes\n", prop->file_size);

	/* file handler */
	char full_path[sizeof(DEST_DIR)+255];
	snprintf(full_path, sizeof(full_path), "%s/%s",
			DEST_DIR, prop->file_name);

	file = open_file(full_path);
	if (file == NULL) {
		perror("open_file");
		goto cleanup1;
	}

	/* receive & write to disk */
	while (total_bytes < prop->file_size) {
		recv_bytes = recv(client_d, content, BUFFER_SIZE, 0);
		if (recv_bytes < 0) {
			perror("\nrecv");
			break;
		}

		if (recv_bytes == 0) {
			errno = ECANCELED;
			perror("\nrecv");
			break;
		}

		writen_bytes = fwrite(content, 1, recv_bytes, file);
		if (ferror(file)) {
			perror("\nfwrite");
			break;
		}

		total_bytes += (uint64_t)writen_bytes;

		print_progress("writing...", total_bytes, prop->file_size);

		if (interrupted == 1)
			break;
	}

	fflush(file);
	fclose(file);

cleanup1:
	putchar('\n');
	close(client_d);
}

int
run_server(int argc, char *argv[])
{
	/*
	 * argv[0] is the bind address
	 * argv[1] is the bind port
	 */

	if (argc != 2) {
		errno = EINVAL;
		print_help(stderr);
		return -errno;
	}

	int socket_d = 0;
	packet_t pkt;

	signal(SIGINT,	interrupt_handler);
	signal(SIGTERM,	interrupt_handler);
	signal(SIGHUP,	interrupt_handler);
	signal(SIGPIPE,	SIG_IGN		 );

	memset(&pkt, 0, sizeof(packet_t));

	printf(WHITE_BOLD_E "Server started [%s:%s]\n" END_E, argv[0], argv[1]);
	printf(WHITE_BOLD_E "Buffer size: %u\n\n" END_E, BUFFER_SIZE);

	if ((socket_d = init_server(argv[0], (uint16_t)atoi(argv[1]))) < 0)
		goto err;

	while (interrupted == 0)
		recv_packet(socket_d, &pkt);

	close(socket_d);

	puts("Stopped");
	return 0;

err:
	fputs("Failed! :(\n", stderr);
	return -errno;
}

