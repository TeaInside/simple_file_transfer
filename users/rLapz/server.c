// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 *
 * NOTE: 
 *       true    : 1,   false  : 0    [ boolean      ]
 *       success : >=0, failed : <0   [ return value ]
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
static int init_server(const char *addr, const uint16_t port);
static int recv_file_prop(const int sock_d, packet_t *prop);
static int get_file_prop(const int sock_d, packet_t *prop,
		struct sockaddr_in *client);
static void recv_packet(const int sock_d);


/* global variables */
extern int is_interrupted;


/* function implementations */
static int
init_server(const char *addr, const uint16_t port)
{
	int sock_opt = 1,
	    sock_d;
	struct sockaddr_in srv;

	if ((sock_d = init_tcp(&srv, addr, port)) < 0) { /* see: ftransfer.c */
		perror("init_server(): socket");
		return -errno;
	}

	/* reuse IP address */
	if (setsockopt(sock_d, SOL_SOCKET, SO_REUSEADDR,
				&sock_opt, sizeof(sock_opt)) < 0) {
		perror("init_server(): setsockopt");
		goto err;
	}

	if (bind(sock_d, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
		perror("init_server(): bind");
		goto err;
	}

	return sock_d;

err:
	close(sock_d);
	return -errno;
}

static int
recv_file_prop(const int sock_d, packet_t *prop)
{
	char *raw_prop = (char *)prop;
	size_t t_bytes = 0,
	       p_size  = sizeof(packet_t);
	ssize_t s_bytes;

	while (t_bytes < p_size && is_interrupted == 0) {
		s_bytes = recv(sock_d, raw_prop + t_bytes, p_size - t_bytes, 0);
		if (s_bytes <= 0)
			break;

		t_bytes += (size_t)s_bytes;
	}

	if (t_bytes != p_size)
		return -errno;

	return 0;
}

static int
get_file_prop(const int sock_d, packet_t *prop, struct sockaddr_in *client)
{
	memset(prop, 0, sizeof(packet_t));

	if (recv_file_prop(sock_d, prop) < 0)
		goto err;

	if (file_check(prop) < 0)
		goto err;

	printf(BOLD_WHITE("File properties [%s:%d]") "\n",
			inet_ntoa(client->sin_addr), ntohs(client->sin_port));
	printf("|-> File name : %s (%u)\n", prop->file_name, prop->file_name_len);
	printf("`-> File size : %" PRIu64 " bytes\n", prop->file_size);

	return 0;

err:
	return -errno;
}

static void
recv_packet(const int sock_d)
{
	FILE     *file_d;
	int       client_d;
	ssize_t   rv_bytes;
	packet_t  prop;
	struct    sockaddr_in client;
	uint64_t  b_total = 0;
	socklen_t client_len = sizeof(struct sockaddr_in);
	char      buffer[BUFFER_SIZE],
	          full_path[sizeof(DEST_DIR) + sizeof(prop.file_name)];

	if (listen(sock_d, 3) < 0) {
		perror("recv_packet(): listening");
		return;
	}

	if (getsockname(sock_d, (struct sockaddr *)&client, &client_len) < 0) {
		perror("recv_packet(): getsockname");
		return;
	}

	client_d = accept(sock_d, (struct sockaddr *)&client, &client_len);
	if (client_d < 0) {
		perror("recv_packet(): accept");
		return;
	}

	/* get file properties */
	if (get_file_prop(client_d, &prop, &client) < 0) {
		perror("recv_packet(): get_file_prop");
		goto cleanup;
	}

	/* file handler */
	snprintf(full_path, sizeof(full_path), "%s/%s", DEST_DIR, prop.file_name);

	if ((file_d = fopen(full_path, "w")) == NULL) {
		perror("recv_packet(): fopen");
		goto cleanup;
	}

	puts("\nWriting...");
	while (b_total < prop.file_size && is_interrupted == 0) {
		rv_bytes = recv(client_d, buffer, sizeof(buffer), 0);
		if (rv_bytes < 0)
			break;
		if (rv_bytes == 0) {
			errno = ECANCELED;
			break;
		}

		b_total += (uint64_t)fwrite(buffer, 1, (size_t)rv_bytes, file_d);

		if (ferror(file_d) != 0)
			break;
	}

	fflush(file_d);
	puts("Buffer flushed");

	fclose(file_d);

	if (errno != 0) {
		perror("recv_packet()");
		goto cleanup;
	}

	if (b_total != prop.file_size) {
		fprintf(stderr, "File \"%s\" is corrupted\n", prop.file_name);
		goto cleanup;
	}

	puts(BOLD_WHITE("Done!"));

cleanup:
	close(client_d);
}

int run_server(int argc, char *argv[])
{
	/*
	 * argv[0] is the bind address
	 * argv[1] is the bind port
	 */

	if (argc != 2) {
		printf("Error: Invalid argument on run_server\n");
		print_help();
		return EINVAL;
	}

	printf(BOLD_WHITE("Server started [%s:%s]") "\n", argv[0], argv[1]);
	printf(BOLD_WHITE("Buffer size: %u") "\n\n", BUFFER_SIZE);

	int    sock_d;
	struct sigaction act;

	if (set_sigaction(&act) < 0) /* see: ftransfer.c */
		goto err;

	uint16_t port = (uint16_t)strtol(argv[1], NULL, 0);
	if ((sock_d = init_server(argv[0], port)) < 0)
		goto err;

	/* main loop */
	while (is_interrupted == 0) {
		errno = 0;
		recv_packet(sock_d);
	}
	/* end main loop */

	close(sock_d);
	puts("Stopped");

	return EXIT_SUCCESS;

err:
	fputs("\nFailed!\n", stderr);
	return EXIT_FAILURE;
}
