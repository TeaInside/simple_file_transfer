// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer client
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
static void interrupt_handler(int sig);
static int init_client(const char *addr, const uint16_t port);
static int set_file_prop(packet_t *prop, char *argv[]);
static int send_file(const int sock_d, char *argv[]);


/* global variables */
static int is_interrupted = 0;


/* function implementations */
static void
interrupt_handler(int sig)
{
	is_interrupted = 1;
	errno = EINTR;

	(void)sig;
}

static int
init_client(const char *addr, const uint16_t port)
{
	int sock_d;
	struct sockaddr_in sock;

	if ((sock_d = init_tcp(&sock, addr, port)) < 0) { /* see: ftransfer.c */
		perror("init_client(): socket");
		return -errno;
	}

	puts("Connecting...");

	if (connect(sock_d, (struct sockaddr *)&sock, sizeof(sock)) < 0) {
		perror("init_client(): connect");
		goto err;
	}

	puts("Connected to the server\n");
	return sock_d;

err:
	close(sock_d);
	return -errno;
}

static int
set_file_prop(packet_t *prop, char *argv[])
{
	char *full_path = argv[2];
	char *base_name;
	struct stat st;

	if (stat(full_path, &st) < 0)
		goto err;

	if (S_ISDIR(st.st_mode)) {
		errno = EISDIR;
		goto err;
	}

	memset(prop, 0, sizeof(packet_t));
	prop->file_size     = (uint64_t)st.st_size;
	prop->file_name_len = (uint8_t)strlen((base_name = basename(full_path)));
	memcpy(prop->file_name, base_name, (size_t)prop->file_name_len);

	if (file_check(prop) < 0) /* see: ftransfer.c */
		goto err;

	puts(BOLD_WHITE("File properties:"));
	printf("|-> Full path   : %s (%zu)\n", full_path, strlen(full_path));
	printf("|-> File name   : %s (%u)\n", prop->file_name, prop->file_name_len);
	printf("|-> File size   : %" PRIu64 " bytes\n", prop->file_size);
	printf("`-> Destination : %s:%s\n", argv[0], argv[1]);

	return 0;

err:
	fprintf(stderr, "\nset_file_prop(): %s: %s\n", argv[2], strerror(errno));
	return -errno;
}


static int
send_file(const int sock_d, char *argv[])
{
	FILE    *file_d;
	ssize_t  s_bytes;
	size_t   r_bytes;
	uint64_t b_total = 0;
	char     buffer[BUFFER_SIZE];
	packet_t prop;

	if (set_file_prop(&prop, argv) < 0)
		return -errno;

	/* open file */
	if ((file_d = fopen(argv[2], "r")) == NULL) {
		perror("send_file(): open");
		return -errno;
	}

	if (file_prop_handler(sock_d, &prop, FUNC_SEND) < 0) {
		perror("send_file(): file_prop_handler");
		goto cleanup;
	}

	puts("\nSending...");
	while (b_total < prop.file_size && is_interrupted == 0) {
		r_bytes = fread((char *)&buffer[0], 1, sizeof(buffer), file_d);
		if (errno != 0)
			break;

		s_bytes = send(sock_d, (char *)&buffer[0], r_bytes, 0);
		if (s_bytes < 0) {
			perror("send");
			break;
		}

		b_total += (uint64_t)s_bytes;
	}

cleanup:
	fclose(file_d);

	if (b_total != prop.file_size) {
		perror(NULL);
		return -errno;
	}

	return 0;
}

int run_client(int argc, char *argv[])
{
	/*
	 * argv[0] is the server address
	 * argv[1] is the server port
	 * argv[2] is the file name
	 */

	if (argc != 3) {
		printf("Error: Invalid argument on run_client\n");
		print_help();
		return EINVAL;
	}

	int    sock_d;
	struct sigaction act;

	if (set_sigaction(&act, interrupt_handler) < 0) /* see: ftransfer.c */
		goto err;

	uint16_t port = (uint16_t)strtol(argv[1], NULL, 0);
	if ((sock_d = init_client(argv[0], port)) < 0)
		goto err;

	int ret = send_file(sock_d, argv);
	close(sock_d);

	if (ret < 0)
		goto err;

	puts(BOLD_WHITE("Done!"));

	return EXIT_SUCCESS;

err:
	fputs("\nFailed!\n", stderr);
	return EXIT_FAILURE;
}
