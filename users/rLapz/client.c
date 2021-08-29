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

#include <endian.h>
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
static int init_client(const char *addr, const uint16_t port);
static int set_file_prop(packet_t *prop, char *argv[]);
static int send_file_prop(const int sock_d, packet_t *prop);
static int send_file(const int sock_d, char *argv[]);


/* global variables */
extern int is_interrupted;


/* function implementations */
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

	prop->file_size     = htobe64((uint64_t)st.st_size);
	prop->file_name_len = (uint8_t)strlen((base_name = basename(full_path)));
	memcpy(prop->file_name, base_name, (size_t)prop->file_name_len);

	if (file_check(prop) < 0) /* see: ftransfer.c */
		goto err;

	puts(BOLD_WHITE("File properties:"));
	printf("|-> Full path   : %s (%zu)\n", full_path, strlen(full_path));
	printf("|-> File name   : %s (%u)\n", prop->file_name, prop->file_name_len);
	printf("|-> File size   : %zu bytes\n", st.st_size);
	printf("`-> Destination : %s:%s\n", argv[0], argv[1]);

	return 0;

err:
	fprintf(stderr, "\nset_file_prop(): %s: %s\n", argv[2], strerror(errno));
	return -errno;
}

static int
send_file_prop(const int sock_d, packet_t *prop)
{
	char *raw_prop = (char *)prop;
	size_t t_bytes = 0,
	       p_size  = sizeof(packet_t);
	ssize_t s_bytes;

	while (t_bytes < p_size && is_interrupted == 0) {
		s_bytes = send(sock_d, raw_prop + t_bytes, p_size - t_bytes, 0);
		if (s_bytes <= 0)
			break;

		t_bytes += (size_t)s_bytes;
	}

	if (t_bytes != p_size)
		return -errno;

	return 0;
}

static int
send_file(const int sock_d, char *argv[])
{
	FILE    *file_d;
	ssize_t  s_bytes;
	size_t   r_bytes;
	uint64_t b_total = 0,
		 p_size  = 0;
	char     buffer[BUFFER_SIZE];
	packet_t prop = {0};

	if (set_file_prop(&prop, argv) < 0)
		return -errno;

	/* open file */
	if ((file_d = fopen(argv[2], "r")) == NULL) {
		perror("send_file(): open");
		return -errno;
	}

	if (send_file_prop(sock_d, &prop) < 0) {
		perror("send_file(): file_prop_handler");
		goto cleanup;
	}
	p_size = be64toh(prop.file_size);

	puts("\nSending...");
	while (b_total < p_size && is_interrupted == 0) {
		r_bytes = fread(buffer, 1, sizeof(buffer), file_d);

		if ((s_bytes = send(sock_d, buffer, r_bytes, 0)) < 0)
			break;

		b_total += (uint64_t)s_bytes;

		if (feof(file_d) != 0 || ferror(file_d) != 0)
			break;
	}

cleanup:
	fclose(file_d);

	if (b_total != p_size) {
		perror("send_file()");
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

	if (set_sigaction(&act) < 0) /* see: ftransfer.c */
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
