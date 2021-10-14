// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer client (IPv4 and IPv6)
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
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


struct client {
	int           tcp_fd   ;
	const char   *addr     ;
	const char   *port     ;
	char         *file_path;
	uint64_t      file_size;
	union pkt_uni pkt      ;
};


/* function declarations */
static void connect_to_server(struct client *c)               ;
static void set_file_prop    (struct client *c)               ;
static int  send_all         (const char *buffer,
			      size_t *size, const int sock_fd);
static void send_file_prop   (struct client *c)               ;
static void send_file        (struct client *c)               ;


/* global variables */
extern int is_interrupted;


/*function implementations */
static void
connect_to_server(struct client *c)
{
	int ret = 0, rv;
	struct addrinfo hints = {0}, *ai, *p = NULL;

	hints.ai_family   = AF_UNSPEC;   /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* TCP */

	if ((rv = getaddrinfo(c->addr, c->port, &hints, &ai)) != 0) {
		FPERROR("connect_to_server(): getaddrinfo: %s\n",
			gai_strerror(rv));

		goto btm;
	}

	for (p = ai; p != NULL; p = p->ai_next) {
		ret = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

		if (ret < 0) {
			PERROR("connect_to_server(): socket");

			continue;
		}

		if (connect(ret, p->ai_addr, p->ai_addrlen) < 0) {
			PERROR("connect_to_server(): connect");
			close(ret);

			continue;
		}

		break;
	}

	freeaddrinfo(ai);

btm:
	if (p == NULL) {
		FPERROR("connect_to_server(): Failed to connect\n");

		exit(1);
	}

	INFO("Connected to server\n");

	c->tcp_fd = ret;
}


static void
set_file_prop(struct client *c)
{
	char  *base_name;
	struct stat st;

	if (stat(c->file_path, &st) < 0)
		goto err;

	if (S_ISDIR(st.st_mode)) {
		errno = EISDIR;

		goto err;
	}

	base_name                 = basename(c->file_path);
	c->file_size              = (uint64_t)st.st_size;
	c->pkt.prop.file_size     = htobe64(c->file_size);
	c->pkt.prop.file_name_len = (uint8_t)strlen(base_name);

	memcpy(c->pkt.prop.file_name, base_name, c->pkt.prop.file_name_len);

	if (file_check(&(c->pkt.prop)) < 0) /* see: ftransfer.c */
		goto err;

	return;

err:
	FPERROR("set_file_prop(): File \"%s\": %s\n",
		c->file_path, strerror(errno));

	exit(1);
}


static int
send_all(const char *buffer, size_t *size, const int sock_fd)
{
	size_t b_total = 0;
	ssize_t b_sent = 0;

	while (b_total < (*size) && is_interrupted == 0) {
		b_sent = send(sock_fd, buffer + b_total, (*size) - b_total, 0);

		if (b_sent < 0) {
			PERROR("send_all(): send");

			break;
		}

		b_total += (size_t)b_sent;
	}

	(*size) = b_total;

	if (b_sent < 0)
		return -1;

	return 0;
}


static void
send_file_prop(struct client *c)
{
	size_t prop_size = sizeof(packet_t);

	if (send_all(c->pkt.raw, &prop_size, c->tcp_fd) < 0 ||
					sizeof(packet_t) != prop_size) {
		FPERROR("send_file_prop(): Failed to send file properties");

		close(c->tcp_fd);
		exit(1);
	}
}


static void
send_file(struct client *c)
{
	FILE    *file_fd;
	size_t   b_read;
	uint64_t b_total = 0;


	/* open file */
	if ((file_fd = fopen(c->file_path, "r")) == NULL) {
		FPERROR("send_file(): File \"%s\": %s\n",
			c->file_path, strerror(errno));

		close(c->tcp_fd);
		exit(1);
	}

	memset(&(c->pkt), 0, sizeof(union pkt_uni));

	INFO("Sending... \n");
	while (b_total < (c->file_size) && is_interrupted == 0) {
		b_read = fread(c->pkt.raw, sizeof(char), BUFFER_SIZE, file_fd);

		if (send_all(c->pkt.raw, (size_t *)&b_read, c->tcp_fd) < 0)
			break;

		b_total += (uint64_t)b_read;

		if (feof(file_fd) != 0)
			break;

		if (ferror(file_fd) != 0) {
			PERROR("send_file(): fread");

			break;
		}
	}

	fclose(file_fd);
	close(c->tcp_fd);

	if (b_total != (c->file_size)) {
		FPERROR("File size did not match!\n"
			"Sent: %" PRIu64 " bytes\n", b_total);

		exit(1);
	}

	if (errno != 0) {
		PERROR("send_file()");

		exit(1);
	}
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
		PERROR("run_client()");
		print_help();

		return EINVAL;
	}

	struct client client = {
		.addr      = argv[0],
		.port      = argv[1],
		.file_path = argv[2]
	};

	set_signal(); /* see: ftransfer.c */
	set_file_prop(&client);


	printf(BOLD_YELLOW(
		"File properties") "\n"
		"|-> Full path   : %s\n"
		"|-> File name   : %s\n"
		"|-> File size   : %" PRIu64 " bytes\n"
		"|-> Destination : %s (%s)\n"
		"`-> Buffer size : %u bytes\n\n",
		client.file_path, client.pkt.prop.file_name, client.file_size,
		client.addr, client.port, BUFFER_SIZE
	);


	connect_to_server(&client);
	send_file_prop(&client);
	send_file(&client);

	INFO("Done!\n");

	return EXIT_SUCCESS;
}
