// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer client
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 *
 * NOTE: true = 1, false = 0
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "ftransfer.h"

/* function declarations */
static void  interrupt_handler (int sig);
static int   get_file_prop     (packet_t *pkt, char *argv[]);
static FILE *open_file         (const char *file_name);
static int   init_client       (const char *addr, const uint16_t port);
static void  send_packet       (const int client_d, const packet_t *prop,
		                     const char *file_name);
int          run_client        (int argc, char *argv[]);

/* global variables */
static volatile int interrupted = 0;


/* function implementations */
static void
interrupt_handler(int sig)
{
	interrupted = 1;
	errno       = EINTR;
	(void)sig;
}

static int
get_file_prop(packet_t *pkt, char *argv[])
{
	const char *base_name;
	char       *full_path;
	struct      stat s;
	size_t      f_len;

	if (stat(argv[2], &s) < 0) {
		perror(argv[2]);
		goto err;
	}

	if (S_ISDIR(s.st_mode)) {
		errno = EISDIR;
		perror(argv[2]);
		goto err;
	}

	full_path = argv[2];
	base_name = basename(full_path);
	f_len	  = strlen(base_name);

	pkt->file_size = (uint64_t)s.st_size;
	pkt->file_name_len = (uint8_t)f_len;

	memcpy(pkt->file_name, base_name, (size_t)pkt->file_name_len);
	pkt->file_name[pkt->file_name_len] = '\0';

	puts(WHITE_BOLD_E "File info" END_E);
	printf("|-> Full path   : %s (%zu)\n",	full_path, strlen(full_path));
	printf("|-> File name   : %s (%u)\n",	pkt->file_name,	pkt->file_name_len);
	printf("|-> File size   : %lu bytes\n", pkt->file_size);
	printf("`-> Destination : %s:%s\n",	argv[0], argv[1]);


	return 0;

err:
	return -errno;
}

static FILE *
open_file(const char *file_name)
{
	FILE *f = fopen(file_name, "r");
	if (f == NULL)
		perror(file_name);

	return f;
}

static int
init_client(const char *addr, const uint16_t port)
{
	int socket_d;
	struct sockaddr_in sock;

	socket_d = init_socket(&sock, addr, port);
	if (socket_d < 0) {
		perror("socket:");
		goto err1;
	}

	printf("\nConnecting...");
	fflush(stdout);

	if (connect(socket_d, (struct sockaddr *)&sock, sizeof(sock)) < 0) {
		perror("\nconnect");
		goto err0;
	}

	puts("\rConnected to the server!\n");
	return socket_d;

err0:
	close(socket_d);

err1:
	puts("\r");
	return -errno;
}

static void
send_packet(const int client_d, const packet_t *prop, const char *file_name)
{
	FILE	*file;
	ssize_t  send_bytes;
	size_t	 read_bytes;
	uint64_t file_size;
	uint64_t bytes_sent = 0;
	char	 content[BUFFER_SIZE];

	/* send file properties */
	send_bytes = send(client_d, prop, sizeof(packet_t), 0);
	if (send_bytes < 0) {
		perror("send");
		return;
	}

	file = open_file(file_name);
	if (file == NULL) {
		perror("open_file");
		return;
	}

	file_size = prop->file_size;

	puts("Sending...");
	/* send the packet */
	while (bytes_sent < file_size && !feof(file)) {
		read_bytes = fread(&content[0], 1, BUFFER_SIZE, file);
		if (ferror(file)) {
			perror("\nfread");
			goto cleanup1;
		}
		
		send_bytes = send(client_d, content, read_bytes, 0);
		if (send_bytes < 0) {
			perror("\nsend");
			goto cleanup1;
		}

		bytes_sent += (uint64_t)send_bytes;

		print_progress(bytes_sent, file_size);

		if (interrupted == 1)
			goto cleanup1;
	}

cleanup1:
	fflush(file);
	fclose(file);
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

	printf(WHITE_BOLD_E "Client started" END_E "\n");
	printf(WHITE_BOLD_E "Buffer size: %u" END_E "\n\n", BUFFER_SIZE);

	int	 socket_d  = 0;
	struct   sigaction act;
	packet_t pkt;

	memset(&act, 0, sizeof(struct sigaction));

	act.sa_handler = interrupt_handler;
	if (sigaction(SIGINT, &act, NULL) < 0)
		goto err;
	if (sigaction(SIGTERM, &act, NULL) < 0)
		goto err;
	if (sigaction(SIGHUP, &act, NULL) < 0)
		goto err;

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) < 0)
		goto err;

	memset(&pkt, 0, sizeof(packet_t));
	if (get_file_prop(&pkt, argv) < 0)
		goto err;

	if ((socket_d = init_client(argv[0], (uint16_t)atoi(argv[1]))) < 0)
		goto err;

	send_packet(socket_d, &pkt, argv[2]);
	close(socket_d);

	if (errno != 0)
		goto err;

	puts("\nuWu :3");
	return 0;

err:
	fputs("\nFailed! :(\n", stderr);
	return -errno;
}

