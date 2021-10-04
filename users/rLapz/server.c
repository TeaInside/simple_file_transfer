
// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server (IPv4 and IPv6)
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "ftransfer.h"



struct client { /* <-----------------------------. */
	uint16_t port;                       /*  | */
	int      fd;                         /*  | */
	struct   sockaddr_storage addr;      /*  | */
	char     addr_str[INET6_ADDRSTRLEN]; /*  | */
};                                           /*  | */
                                             /*  | */
struct server {                              /*  | */
	int listener;                        /*  | */
                                             /*  | */
	struct sockaddr_storage *addr;       /*  | */
	struct client *client; /* >--------------' */
	struct {
		int fd_count, fd_size, poll_count;
		struct pollfd *pfds;
	} fds;
};



/* function declarations */
static int         get_listener(const char *addr, const char *port);
static int         add_to_pfds(struct server *s, const int new_fd);
static void        server_poll(struct server *s);
static void        delete_from_pfds(struct server *s, const int index);
static const char *get_addr_str(char *dest, const struct sockaddr *s);
static uint16_t    get_port(const struct sockaddr *s);
static int         recv_all(char *buffer, size_t *size, const int client_fd);
static int         get_file_prop(const struct client *c, packet_t *prop);
static void        file_io(const struct server *s, const int index);
static void        clean_up(struct server *s);


/* global variables */
extern int is_interrupted;


/* function implementations */
static int
get_listener(const char *addr, const char *port)
{
	int ret, rv, yes = 1;
	struct addrinfo hints = {0}, *ai, *p;

	hints.ai_family   = AF_UNSPEC;   /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* TCP */

	if ((rv = getaddrinfo(addr, port, &hints, &ai)) != 0) {
		fprintf(stderr, "server: get_listener(): getaddrinfo: %s\n",
			gai_strerror(rv)
		);

		return -1;
	}

	for (p = ai; p != NULL; p = p->ai_next) {
		ret = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

		if (ret < 0) {
			perror("server: get_listener(): socket");

			continue;
		}

		if (setsockopt(ret, SOL_SOCKET, SO_REUSEADDR, &yes,
					sizeof(int)) < 0) {
			perror("server: get_listener(): setsockopt");
			close(ret);

			continue;
		}

		if (bind(ret, p->ai_addr, p->ai_addrlen) < 0) {
			perror("server: get_listener(): bind");
			close(ret);

			continue;
		}

		break;
	}

	freeaddrinfo(ai);

	if (p == NULL)
		return -1;

	if (listen(ret, 10) < 0) {
		perror("server: get_listener(): listen");

		return -1;
	}

	return ret;
}


static int
add_to_pfds(struct server *s, const int new_fd)
{
	struct pollfd *tmp_srv;
	struct client *tmp_client;

	/* Resizing pfds and client array */
	if (s->fds.fd_count == s->fds.fd_size) {
		s->fds.fd_size *= 2; /* Double it */

		tmp_srv = realloc(s->fds.pfds,
				sizeof(struct pollfd) * s->fds.fd_size);

		if (tmp_srv == NULL) {
			perror("server: add_to_pfds(): malloc for fds");

			return -1;
		}

		tmp_client = realloc(s->client,
				sizeof(struct client) * s->fds.fd_size);

		if (tmp_client == NULL) {
			perror("server: add_to_pfds(): malloc for client");

			return -1;
		}

		s->fds.pfds = tmp_srv;
		s->client   = tmp_client;
	}

	int i = s->fds.fd_count;

	s->fds.pfds[i].fd     = new_fd;
	s->fds.pfds[i].events = POLLIN;

	s->client[i].fd   = s->fds.pfds[i].fd;
	s->client[i].addr = *(s->addr);         /* copy the value */
	s->client[i].port = htons(get_port((struct sockaddr *)&(s->client[i].addr)));

	get_addr_str(s->client[i].addr_str, (struct sockaddr *)&(s->client[i].addr));


	printf("server: new connection from %s:%d on socket %d\n\n",
			s->client[i].addr_str, s->client[i].port, new_fd
	);

	(s->fds.fd_count)++;

	return s->fds.fd_count;
}


static void
server_poll(struct server *s)
{
	int       new_fd;
	struct    sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);

	s->addr           = &addr;
	s->fds.poll_count = poll(s->fds.pfds, s->fds.fd_count, -1);

	if (s->fds.poll_count == -1)
		perror("server: server_poll(): poll");

	for (int i = 0; i < s->fds.fd_count; i++) {
		if (s->fds.pfds[i].revents != POLLIN)
			continue;

		if (s->fds.pfds[i].fd == s->listener) {
			new_fd = accept(s->listener,
					(struct sockaddr *)&addr, &addr_len);

			if (new_fd < 0) {
				perror("server: server_poll(): accept");

				continue;
			}

			if (add_to_pfds(s, new_fd) < 0) {
				clean_up(s);
				die("server: server_poll()");
			}

		} else {
			/* TODO: Multithreading support (file I/O) */
	
			file_io(s, i);
	
			close(s->fds.pfds[i].fd);
	
			printf("server: client on socket %d has closed\n",
					s->fds.pfds[i].fd
			);
	
			delete_from_pfds(s, i);
		}
	}
}


static void
delete_from_pfds(struct server *s, const int index)
{
	s->fds.pfds[index] = s->fds.pfds[s->fds.fd_count -1];
	s->client[index]   = s->client[s->fds.fd_count -1];

	(s->fds.fd_count)--;
}


static const char *
get_addr_str(char *dest, const struct sockaddr *s)
{
	void *so; 

	if (s->sa_family == AF_INET)
		so = &(((struct sockaddr_in *)s)->sin_addr);   /* IPv4 */
	else
		so = &(((struct sockaddr_in6 *)s)->sin6_addr); /* IPv6 */

	return inet_ntop(s->sa_family, (struct sockaddr *)so, dest, INET6_ADDRSTRLEN);
}


static uint16_t
get_port(const struct sockaddr *s)
{
	if (s->sa_family == AF_INET)
		return ((struct sockaddr_in *)s)->sin_port;   /* IPv4 */

	return ((struct sockaddr_in6 *)s)->sin6_port;         /* IPv6 */
}


static int
recv_all(char *buffer, size_t *size, const int client_fd)
{
	size_t  b_total = 0;
	size_t  b_left  = *size;
	ssize_t b_recv;

	while (b_total < (*size) && is_interrupted == 0) {
		b_recv = recv(client_fd, buffer + b_total, b_left, 0);

		if (b_recv < 0) {
			perror("server: recv_all(): recv");

			break;
		}

		if (b_recv == 0)
			break;

		b_total += (size_t)b_recv;
		b_left  -= (size_t)b_recv;
	}

	(*size) = b_total;

	if (b_recv < 0)
		return -1;

	return 0;
}


static int
get_file_prop(const struct client *c, packet_t *prop)
{
	char   *raw       = (char *)prop;
	size_t  prop_size = sizeof(packet_t);
	size_t  tmp_size  = sizeof(packet_t);

	if (recv_all(raw, &prop_size, c->fd) < 0 ||
			tmp_size != prop_size) {

		fprintf(stderr, 
			"File properties is corrupted or it's size did not match!\n"
		);

		return -1;
	}

	if (file_check(prop) < 0) {
		fprintf(stderr, "server: get_file_prop(): Invalid file name\n");
		
		return -1;
	}


	printf(BOLD_WHITE("File properties [%s:%d] on socket %d") "\n",
			c->addr_str, c->port, c->fd
	);
	printf("|-> File name : %s (%u)\n",
			prop->file_name, prop->file_name_len
	);
	printf("`-> File size : %" PRIu64 " bytes\n",
			be64toh(prop->file_size)
	);


	return 0;
}

static void
file_io(const struct server *s, const int index)
{
	packet_t  prop;
	FILE     *file_fd;
	size_t    b_recv;
	uint64_t  b_total = 0;
	uint64_t  f_size;

	char      buffer[BUFFER_SIZE];
	char      full_path[sizeof(DEST_DIR) + sizeof(prop.file_name)];


	memset(&prop, 0, sizeof(packet_t));

	if (get_file_prop(&(s->client[index]), &prop) < 0)
		return;

	f_size = be64toh(prop.file_size);

	if (snprintf(full_path, sizeof(full_path), "%s/%s",
			DEST_DIR, prop.file_name) < 0) {

		perror("server: file_io(): set full path");

		return;
	}

	if ((file_fd = fopen(full_path, "w")) == NULL) {
		fprintf(stderr, "server: file_io(): fopen: \"%s\": %s\n",
				full_path, strerror(errno)
		);

		return;
	}

	puts("\nWriting...");
	while (b_total < f_size && is_interrupted == 0) {
		if (recv_all(buffer, &b_recv, s->client[index].fd) < 0)
			break;

		b_total += (uint64_t)fwrite(buffer, 1, b_recv, file_fd);

		if (ferror(file_fd) != 0) {
			perror("server: file_io(): fwrite");

			break;
		}
	}

	fflush(file_fd);
	puts("Buffer flushed");

	fclose(file_fd);

	if (b_total != f_size) {
		fprintf(stderr, 
			"File \"%s\" is corrupted or file size did not match!\n",
			prop.file_name
		);

		return;
	}

	puts("Done!\n");
}


static void
clean_up(struct server *s)
{
	close(s->listener);

	if (s->fds.pfds != NULL) {
		free(s->fds.pfds);
		s->fds.pfds = NULL;
	}

	if (s->client != NULL) {
		free(s->client);
		s->client = NULL;
	}
}



int
run_server(int argc, char *argv[])
{
	/*
	 * argv[0] is the bind address
	 * argv[1] is the bind port
	 */

	if (argc != 2) {
		printf("server: Invalid argument on run_server\n");
		print_help();

		return EINVAL;
	}

	printf(BOLD_WHITE("Server started [%s:%s]") "\n", argv[0], argv[1]);
	printf(BOLD_WHITE("Buffer size: %u") "\n\n", BUFFER_SIZE);


	/* --------------------------------------------- */

	struct server srv = {0};

	set_signal(); /* see: ftransfer.c */

	srv.fds.fd_size = 5;
	srv.fds.pfds    = calloc(sizeof(struct pollfd),
				sizeof(struct pollfd) * srv.fds.fd_size);
	srv.client      = calloc(sizeof(struct client),
				sizeof(struct client) * srv.fds.fd_size);

	if (srv.fds.pfds == NULL)
		die("server: run_server(): malloc for fds");

	if (srv.client == NULL)
		die("server: run_server(): malloc for client");

	if ((srv.listener = get_listener(argv[0], argv[1])) < 0) {
		free(srv.fds.pfds);

		return EXIT_FAILURE;
	}

	srv.fds.pfds[0].fd     = srv.listener;
	srv.fds.pfds[0].events = POLLIN;
	srv.fds.fd_count       = 1;

	/* MAIN LOOP */
	while (is_interrupted == 0) {
		errno = 0;
		server_poll(&srv);
	}

	clean_up(&srv);

	puts("server: Stopped");

	if (errno != 0 && errno != EINTR)
		return EXIT_FAILURE;

	puts("server: server has been stopped gracefully. :3");

	return EXIT_SUCCESS;
}

