// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer client (IPv4 and IPv6)
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 *
 * NOTE: 
 *       true    : 1,   false  : 0    [ boolean      ]
 *       success : >=0, failed : <0   [ return value ]
 */

#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netdb.h>

#include "ftransfer.h"


/* function declarations */
static int connect_to_server(const char *addr, const char *port);
static int set_file_prop(packet_t *prop, char *argv[]);
static int send_file_prop(packet_t *prop, const int sock_fd);
static int send_file(const int sock_fd, packet_t *prop, char *argv[]);


/* global variables */
extern int is_interrupted;


/*function implementations */
static int
connect_to_server(const char *addr, const char *port)
{
	int ret, rv;
	struct addrinfo hints = {0}, *ai, *p;

	hints.ai_family   = AF_UNSPEC;   /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* TCP */

	if ((rv = getaddrinfo(addr, port, &hints, &ai)) != 0) {
		fprintf(stderr, "client: connect_to_server(): getaddrinfo: %s\n",
				gai_strerror(rv)
		);

		return -1;
	}

	for (p = ai; p != NULL; p = p->ai_next) {
		ret = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (ret < 0) {
			perror("client: connect_to_server(): socket");

			continue;
		}

		if (connect(ret, p->ai_addr, p->ai_addrlen) < 0) {
			close(ret);
			perror("client: connect_to_server(): connect");

			continue;
		}

		break;
	}

	freeaddrinfo(ai);

	if (p == NULL)
		return -1;

	puts("Connected to server\n");

	return ret;
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
	printf("|-> File size   : %" PRIu64 " bytes\n", st.st_size);
	printf("`-> Destination : %s:%s\n", argv[0], argv[1]);

	return 0;

err:
	fprintf(stderr, "client: set_file_prop(): \"%s\": %s\n",
			full_path, strerror(errno)
	);

	return -1;
}


static int
send_all(const int sock_fd, const char *buffer, size_t *size)
{
	size_t b_total = 0;
	size_t b_left  = *size;
	ssize_t b_sent;

	while (b_total < (*size) && is_interrupted == 0) {
		b_sent = send(sock_fd, buffer +b_total, b_left, 0);

		if (b_sent < 0) {
			perror("client: send_all(): send");

			break;
		}

		if (b_sent == 0)
			break;

		b_total += (size_t)b_sent;
		b_left  -= (size_t)b_sent;
	}

	(*size) = b_total;

	if (b_sent < 0)
		return -1;

	return 0;
}


static int
send_file_prop(packet_t *prop, const int sock_fd)
{
	char *raw = (char *)prop;
	size_t prop_size = sizeof(packet_t);
	size_t tmp_size  = sizeof(packet_t);

	if (send_all(sock_fd, raw, &prop_size) < 0 || tmp_size != prop_size) {
		fprintf(stderr,
			"client: send_file_prop(): Error when sending file prop"
		);

		return -1;
	}

	return 0;
}


static int
send_file(const int sock_fd, packet_t *prop, char *argv[])
{
	FILE    *file_fd;
	char     buffer[BUFFER_SIZE];
	size_t   b_read;
	uint64_t b_total = 0;
	uint64_t f_size  = 0;
	size_t   buffer_size = sizeof(buffer);

	/* open file */
	if ((file_fd = fopen(argv[2], "r")) == NULL) {
		perror("client: send_file(): open");

		return -1;
	}

	if (send_file_prop(prop, sock_fd) < 0)
		return -1;

	f_size = be64toh(prop->file_size);

	puts("Sending...");
	while (b_total < f_size) {
		b_read = fread(buffer, 1, buffer_size, file_fd);

		if (send_all(sock_fd, buffer, (size_t *)&b_read) < 0)
			break;

		b_total += (uint64_t)b_read;

		if (feof(file_fd) != 0)
			break;

		if (ferror(file_fd) != 0) {
			perror("client: send_file(): fread");

			break;
		}
	}

	fflush(file_fd);
	fclose(file_fd);

	if (b_total != f_size) {
		fprintf(stderr, 
			"File \"%s\" was corrupted or file size did not match!\n",
			argv[2]
		);

		return -1;
	}

	return 0;
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
		printf("Error: Invalid argument on run_client\n");
		print_help();

		return EINVAL;
	}

	int sock_fd;
	packet_t prop = {0};

	set_signal(); /* see: ftransfer.c */

	if (set_file_prop(&prop, argv) < 0)
		return EXIT_FAILURE;

	if ((sock_fd = connect_to_server(argv[0], argv[1])) < 0)
		return EXIT_FAILURE;

	int ret = send_file(sock_fd, &prop, argv);

	close(sock_fd);

	if (ret < 0) {
		fputs("\nFailed!\n", stderr);

		return EXIT_FAILURE;
	}

	puts(BOLD_WHITE("Done!"));

	return EXIT_SUCCESS;
}
