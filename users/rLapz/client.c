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
#include <sys/stat.h>

#include "ftransfer.h"

/* function declarations */
static void  interrupt_handler (int sig);
static int   init_client       (const char *addr, const uint16_t port);
static void  send_packet       (const int socket_d, char *argv[]);
static int   get_file_prop     (packet_t *prop, char *argv[]);

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
init_client(const char *addr, const uint16_t port)
{
	int socket_d;
	struct sockaddr_in sock;

	socket_d = init_socket(&sock, addr, port); /* see: ftransfer.c */
	if (socket_d < 0) {
		perror("\nsocket:");
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
send_packet(const int socket_d, char *argv[])
{
	int      file;
	ssize_t  sent_bytes,
		 read_bytes;
	uint64_t file_size,
		 total_bytes;
	char    *file_path,
		 content[BUFFER_SIZE];
	packet_t prop;

	/* get file properties */
	if (get_file_prop(&prop, argv) < 0)
		return;

	file_path   = argv[2];
	file_size   = prop.file_size;
	total_bytes = 0;

	puts("\nSending...");
	/* send file properties */
	sent_bytes = send(socket_d, (packet_t *)&prop, sizeof(packet_t), 0);
	if (sent_bytes < 0) {
		perror("send");
		return;
	}

	/* open file */
	if ((file = open(file_path, O_RDONLY)) < 0) {
		perror(prop.file_name);
		return;
	}

	while (total_bytes < file_size && is_interrupted == 0) {
		read_bytes = read(file, (char*)&content[0], BUFFER_SIZE);
		if (read_bytes < 0) {
			perror("\nread");
			break;
		}

		sent_bytes = send(socket_d, (char*)&content[0],
						(size_t)read_bytes, 0);
		if (sent_bytes < 0) {
			perror("\nsend");
			break;
		}

		total_bytes += (uint64_t)sent_bytes;
	}

	printf("\n\rFlushing buffer...");
	fflush(stdout);
	
	fsync(file);
	close(file);

	puts(" - " WHITE_BOLD_E "Done!" END_E);
}

static int
get_file_prop(packet_t *prop, char *argv[])
{
	const char *base_name;
	char       *full_path;
	struct      stat s;
	size_t      bn_len;

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
	bn_len	  = strlen(base_name);

	memset(prop, 0, sizeof(packet_t));

	prop->file_size     = (uint64_t)s.st_size;
	prop->file_name_len = (uint8_t)bn_len;
	memcpy(prop->file_name, base_name, (size_t)prop->file_name_len);

	if (file_verif(prop) < 0) {	/* see: ftransfer.c */
		fputs("Invalid file name! :p\n\n", stderr);
		goto err;
	}

	puts(WHITE_BOLD_E "File info" END_E);
	printf("|-> Full path   : %s (%zu)\n", full_path, strlen(full_path));
	printf("|-> File name   : %s (%u)\n", prop->file_name, prop->file_name_len);
	printf("|-> File size   : %lu bytes\n", prop->file_size);
	printf("`-> Destination : %s:%s\n", argv[0], argv[1]);

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

	printf(WHITE_BOLD_E "Client started" END_E "\n");
	printf(WHITE_BOLD_E "Buffer size: %u" END_E "\n\n", BUFFER_SIZE);

	int	 socket_d;
	struct   sigaction act;

	if (set_sigaction(&act, interrupt_handler) < 0) /* see: ftransfer.c */
		goto err0;

	if ((socket_d = init_client(argv[0], (uint16_t)atoi(argv[1]))) < 0)
		goto err1;

	send_packet(socket_d, argv);
	close(socket_d);

	if (errno != 0)
		goto err1;

	puts("\nuWu :3");
	return 0;

err0:
	perror(NULL);

err1:
	fputs("\nFailed! :(\n", stderr);
	return -errno;
}

