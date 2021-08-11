// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 *
 * NOTE: true = 1, false = 0
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include "ftransfer.h"

/* function declarations */
static void  interrupt_handler (int sig);
static int   init_server       (const char *addr, const uint16_t port);
static int   get_file_prop     (const int socket_d, packet_t *prop,
					struct sockaddr_in *client);
static void  recv_packet       (const int socket_d);

/* applying the configuration */
#include "config.h"

/* global variables */
static volatile int is_interrupted = 0;

/* function implementations */
static void
interrupt_handler(int sig)
{
	is_interrupted = 1;
	errno          = EINTR;

	(void)sig;
}

static int
init_server(const char *addr, const uint16_t port)
{
	int    sock_opt = 1,
	       socket_d;
	struct sockaddr_in srv;

	socket_d = init_socket(&srv, addr, port); /* see: ftransfer.c */
	if (socket_d < 0) {
		perror("socket");
		return -errno;
	}

	/* socket option
	 * reuse IP address */
	if (setsockopt(socket_d, SOL_SOCKET, SO_REUSEADDR,
				&sock_opt, sizeof(sock_opt)) < 0) {
		perror("setsockopt");
		goto err;
	}

	if (bind(socket_d, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
		perror("bind");
		goto err;
	}

	return socket_d;

err:
	close(socket_d);
	return -errno;
}

static int
get_file_prop(const int client_d, packet_t *prop, struct sockaddr_in *client)
{
	ssize_t recv_bytes;
	size_t  total_bytes = 0,
		len_prop    = sizeof(packet_t);
	char   *raw_prop    = (char*)prop;

	puts("Receiving file properties...\n");

	memset(prop, 0, len_prop);

	while (total_bytes < len_prop && is_interrupted == 0) {
		recv_bytes = recv(client_d, 
				(char *)raw_prop + total_bytes,
				len_prop - total_bytes, 0);

		if (recv_bytes < 0) {
			perror("recv");
			return -errno;
		}

		if (recv_bytes == 0) {
			errno = ECANCELED;
			perror("recv");
			return -errno;
		}

		total_bytes += (size_t)recv_bytes;
	}

	/* when the looping was interrupted */
	if (total_bytes < len_prop)
		return -errno;

	if (file_verif(prop) < 0) {	/* see: ftransfer.c */
		fputs("Invalid file name! :p\n\n", stderr);
		return -errno;
	}

	printf(WHITE_BOLD_E "File info [%s:%d]" END_E "\n",
			inet_ntoa(client->sin_addr), ntohs(client->sin_port));
	printf("|-> File name   : %s (%u)\n", prop->file_name, prop->file_name_len);
	printf("`-> File size   : %" PRIu64 " bytes\n", prop->file_size);

	return 0;
}

static void
recv_packet(const int socket_d)
{
	int       file,
		  client_d;
	ssize_t   recv_bytes,
		  writen_bytes;
	packet_t  prop;
	mode_t    file_mode;
	struct    sockaddr_in client;
	uint64_t  file_size,
		  total_bytes	= 0;
	socklen_t client_len	= sizeof(struct sockaddr_in);
	char      content[BUFFER_SIZE],
		  full_path[sizeof(DEST_DIR) + sizeof(prop.file_name)];
	
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

	/* get file properties from client */
	if (get_file_prop(client_d, &prop, &client) < 0)
		goto cleanup;

	file_size = prop.file_size;

	/* file handler */
	snprintf(full_path, sizeof(full_path), "%s/%s", DEST_DIR, prop.file_name);

	file_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	if ((file = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, file_mode)) < 0) {
		perror("open_file");
		goto cleanup;
	}

	puts("\nwriting...");
	/* receive & write to disk */
	while (total_bytes < file_size && is_interrupted == 0) {
		recv_bytes = recv(client_d, (char *)&content[0], BUFFER_SIZE, 0);
		if (recv_bytes < 0) {
			perror("\nrecv");
			break;
		}

		if (recv_bytes == 0) {
			errno = ECANCELED;
			perror("\nrecv");
			break;
		}

		writen_bytes = write(file, (char *)&content[0], (size_t)recv_bytes);
		if (writen_bytes < 0) {
			perror("\nfwrite");
			break;
		}

		total_bytes += (uint64_t)writen_bytes;
	}

	puts("Flushing buffer...");

	fsync(file);
	close(file);

	puts(WHITE_BOLD_E "Done!" END_E);

cleanup:
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

	int	 socket_d;
	struct   sigaction act;

	if (set_sigaction(&act, interrupt_handler) < 0) /* see: ftransfer.c */
		goto err;

	if ((socket_d = init_server(argv[0], (uint16_t)atoi(argv[1]))) < 0)
		goto err;

	/* main loop */
	while (is_interrupted == 0)
		recv_packet(socket_d);
	/* end main loop */

	close(socket_d);

	puts("Stopped");
	return 0;

err:
	fputs("Failed! :(\n", stderr);
	return -errno;
}

