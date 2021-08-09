// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 *
 * NOTE: true = 1, false = 0
 */
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ftransfer.h"

/* function declarations */
static void  interrupt_handler (int sig);
static int   file_verif        (const char *filename);
static int   init_server       (const char *addr, const uint16_t port);
static void  recv_packet       (const int socket_d);
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
	int    sock_opt = 1;
	int    socket_d;
	struct sockaddr_in srv;

	socket_d = init_socket(&srv, addr, port); /* see: ftransfer.h */
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
recv_packet(const int socket_d)
{
	int       file;
	int       client_d;
	ssize_t   recv_bytes;
	ssize_t   writen_bytes;
	struct    sockaddr_in client;
	char      content[BUFFER_SIZE];
	packet_t  prop;
	uint64_t  file_size;
	uint64_t  total_bytes	= 0;
	socklen_t client_len	= sizeof(struct sockaddr_in);
	char      full_path[sizeof(DEST_DIR) + sizeof(prop.file_name) +2];
	
	if (listen(socket_d, 3) < 0) {
		perror("listening");
		return;
	}

	if (getsockname(socket_d, (struct sockaddr *)&client, &client_len) < 0) {
		perror("getsockname");
		return;
	}

	/* accept client connection */
	client_d = accept(socket_d, (struct sockaddr *)&client, &client_len);
	if (client_d < 0) {
		perror("accept");
		return;
	}

	memset(&prop, 0, sizeof(packet_t));
	/* get file properties from client */
	puts("Receiving file properties...\n");
	recv_bytes = recv(client_d, (packet_t *)&prop, sizeof(packet_t), 0);
	if (recv_bytes < 0) {
		perror("recv");
		goto cleanup1;
	}

	if (file_verif(prop.file_name) < 0) {
		fputs("Invalid file name! :p\n\n\n", stderr);
		goto cleanup1;
	}

	file_size = prop.file_size;

	printf(WHITE_BOLD_E "File info [%s:%d]" END_E "\n",
			inet_ntoa(client.sin_addr), ntohs(client.sin_port));
	printf("|-> File name   : %s (%u)\n",
			prop.file_name, prop.file_name_len);
	printf("`-> File size   : %lu bytes\n", file_size);

	/* file handler */
	snprintf(full_path, sizeof(full_path), "%s/%s", DEST_DIR, prop.file_name);

	file = open(full_path, O_WRONLY | O_CREAT | O_TRUNC,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	if (file < 0) {
		perror("open_file");
		goto cleanup1;
	}

	puts("\nwriting...");
	/* receive & write to disk */
	while (total_bytes < file_size && interrupted == 0) {
		recv_bytes = recv(client_d, (char *)&content[0], BUFFER_SIZE, 0);
		if (recv_bytes < 0) {
			perror("\nrecv");
			break;
		}

		writen_bytes = write(file, (char *)&content[0], (size_t)recv_bytes);
		if (writen_bytes < 0) {
			perror("\nfwrite");
			break;
		}

		total_bytes += (uint64_t)writen_bytes;

		if (recv_bytes == 0) {
			errno = ECANCELED;
			perror("\nrecv");
			break;
		}
	}

	printf("\n\rFlushing buffer...");
	fflush(stdout);

	fsync(file);
	close(file);

	puts(" - " WHITE_BOLD_E "Done!" END_E);

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

	printf(WHITE_BOLD_E "Server started [%s:%s]" END_E "\n", argv[0], argv[1]);
	printf(WHITE_BOLD_E "Buffer size: %u" END_E"\n\n", BUFFER_SIZE);

	int	 socket_d = 0;
	struct   sigaction act;

	memset((struct sigaction *)&act, 0, sizeof(struct sigaction));

	act.sa_handler= interrupt_handler;
	if (sigaction(SIGINT, &act, NULL) < 0)
		goto err;
	if (sigaction(SIGTERM, &act, NULL) < 0)
		goto err;
	if (sigaction(SIGHUP, &act, NULL) < 0)
		goto err;

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) < 0)
		goto err;

	if ((socket_d = init_server(argv[0], (uint16_t)atoi(argv[1]))) < 0)
		goto err;

	/* main loop */
	while (interrupted == 0)
		recv_packet(socket_d);
	/* end main loop */

	close(socket_d);

	puts("Stopped");
	return 0;

err:
	fputs("Failed! :(\n", stderr);
	return -errno;
}

