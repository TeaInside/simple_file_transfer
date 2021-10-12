// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server (IPv4 and IPv6)
 *
 * Copyright (C) 2021  Arthur Lapz <rlapz@gnuweeb.org>
 */

#include <stdbool.h>
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "ftransfer.h"


enum client_stat {
	WAIT,
	DONE,
};


struct client {
	uint16_t         port                    ;
	int              sock_fd                 ;
	bool             got_file_prop           ;
	enum client_stat status                  ;
	uint64_t         file_size               ;
	uint64_t         recvd_bytes             ;
	FILE            *file_fd                 ;
	char             file_name[FILE_NAME_LEN];
	char             addr[INET6_ADDRSTRLEN]  ;
	union pkt_uni    pkt                     ;
};


struct server {
	uint16_t         fd_count;
	uint16_t         fd_size ;
	int              listener;
	int              poll_ret;
	const char      *addr    ;
	const char      *port    ;
	struct client   *clients ;
	struct pollfd   *pfds    ;
};


/* function declarations */
static const char *get_addr     (char *dest, struct sockaddr *sa)  ;
static uint16_t    get_port     (struct sockaddr *sa)              ;

static void        get_file_prop(struct client *c)                 ;
static int         file_prep    (struct client *c)                 ;
static void        file_io      (struct client *c)                 ;

static void        setup_tcp    (struct server *s)                 ;
static void        init_server  (struct server *s, char *argv[])   ;

static int         server_poll  (struct server *s)                 ;
static void        handle_evs   (struct server *s)                 ;
static void        client_acc   (struct server *s)                 ;
static void        client_ev    (struct server *s, const int index);
static int         add_to_pfds  (struct server *s,
				 struct sockaddr_storage *addr,
				 const int new_fd)                 ;
static void        del_from_pfds(struct server *s, const int index);
static void        cleanup      (struct server *s)                 ;



/* global variables */
extern int is_interrupted;



/* function implementations */
static const char *
get_addr(char *dest, struct sockaddr *sa)
{
	void *s;

	if (sa->sa_family == AF_INET)
		s = &(((struct sockaddr_in *)sa)->sin_addr);   /* IPv4 */
	else
		s = &(((struct sockaddr_in6 *)sa)->sin6_addr); /* IPv6 */

	return inet_ntop(sa->sa_family, s, dest, INET6_ADDRSTRLEN);
}


static uint16_t
get_port(struct sockaddr *sa)
{
	uint16_t port;

	if (sa->sa_family == AF_INET)
		port = ((struct sockaddr_in *)sa)->sin_port;   /* IPv4 */
	else
		port = ((struct sockaddr_in6 *)sa)->sin6_port; /* IPv6 */

	return htons(port);
}


static void
get_file_prop(struct client *c)
{
	const size_t p_size = sizeof(packet_t);
	ssize_t      b_recv;

	if (c->recvd_bytes < p_size) {
		b_recv = recv(c->sock_fd, c->pkt.raw + (c->recvd_bytes),
				p_size - (c->recvd_bytes), 0);

		if (b_recv < 0) {
			PERROR("get_file_prop()");

			goto done;
		}

		if (b_recv == 0)
			goto done;

		c->recvd_bytes += (uint64_t)b_recv;

		return;
	}


	if (file_check(&(c->pkt.prop)) < 0) {
		FPERROR("get_file_prop(): Invalid file name\n");

		goto done;
	}

	c->got_file_prop = true;
	c->recvd_bytes   = 0;
	c->file_size     = be64toh(c->pkt.prop.file_size);

	memcpy(c->file_name, c->pkt.prop.file_name, FILE_NAME_LEN);


	printf(BOLD_YELLOW(
		"File properties [%s (%u)] on socket %d") "\n"
		"|-> File name: %s (%u)\n"
		"`-> File size: %" PRIu64 " bytes\n\n",

		c->addr, c->port, c->sock_fd,
		c->file_name, c->pkt.prop.file_name_len, c->file_size
	);

	return;

done:
	c->status      = DONE;
	c->recvd_bytes = 0;
}


static int
file_prep(struct client *c)
{
	char s_path[sizeof(DEST_DIR) + FILE_NAME_LEN];

	if (snprintf(s_path, sizeof(s_path), "%s/%s",
					DEST_DIR, c->file_name) < 0) {

		PERROR("file_prep(): set storage path");

		return -1;
	}

	if ((c->file_fd = fopen(s_path, "w")) == NULL) {
		FPERROR("file_prep(): fopen: \"%s\": %s\n",
			s_path, strerror(errno));

		return -1;
	}

	return 0;
}


static void
file_io(struct client *c)
{
	ssize_t b_recv;
	size_t  b_wr;

	if (c->file_fd == NULL) {
		if (file_prep(c) < 0)
			goto cleanup;
	}

	if (c->recvd_bytes < c->file_size) {
		b_recv = recv(c->sock_fd, c->pkt.raw, BUFFER_SIZE, 0);
		if (b_recv < 0) {
			PERROR("file_io(): recv");

			goto cleanup;
		}

		if (b_recv == 0)
			goto cleanup;

		b_wr = fwrite(c->pkt.raw, 1, (size_t)b_recv, c->file_fd);
		c->recvd_bytes += (uint64_t)b_wr;

		if (ferror(c->file_fd) != 0) {
			PERROR("file_io(): fwrite");

			goto cleanup;
		}

		return;
	}

cleanup:
	if (c->recvd_bytes != c->file_size) {
		FPERROR("File \"%s\" is corrupted or file size did not match!\n"
			BOLD_WHITE("Received: ") "%" PRIu64 " bytes\n\n",
			c->file_name, c->recvd_bytes
		);
	}

	if (c->file_fd != NULL) {
		INFO("Flushing file buffer...\n");
		fflush(c->file_fd);
		fclose(c->file_fd);
		c->file_fd = NULL;
	}

	c->status      = DONE;
	c->recvd_bytes = 0;
}


static void
setup_tcp(struct server *s)
{
	int ret = 0, yes = 1, ga;
	struct addrinfo hints = {0}, *ai, *p = NULL;

	hints.ai_family   = AF_UNSPEC;   /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* TCP */

	if ((ga = getaddrinfo(s->addr, s->port, &hints, &ai)) != 0) {
		FPERROR("set_socket_fd(): getaddrinfo: %s\n",
			gai_strerror(ga));

		goto err;
	}

	for (p = ai; p != NULL; p = p->ai_next) {
		ret = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

		if (ret < 0) {
			PERROR("set_socket_fd(): socket");

			continue;
		}

		/* reuse address */
		if (setsockopt(ret, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) < 0) {

			PERROR("set_socket_fd(): setsockopt");
			close(ret);

			continue;
		}

		if (bind(ret, p->ai_addr, p->ai_addrlen) < 0) {
			PERROR("set_socket_fd(): bind");
			close(ret);

			continue;
		}

		if (listen(ret, BACKLOG) < 0) {
			PERROR("set_socket_fd(): listen");
			close(ret);

			continue;
		}

		break;
	}

	freeaddrinfo(ai);

err:
	if (p == NULL) {
		PERROR("set_socket_fd(): Failed to bind");

		exit(1);
	}

	s->listener = ret;
}


static void
init_server(struct server *s, char *argv[])
{
	s->addr    = argv[0];
	s->port    = argv[1];
	s->fd_size = INIT_CLIENT_SIZE;

	setup_tcp(s);

	if ((s->pfds = malloc(sizeof(struct pollfd) * s->fd_size)) == NULL) {
		PERROR("init_server(): malloc for pfds");

		exit(1);
	}

	if ((s->clients = malloc(sizeof(struct client) * s->fd_size)) == NULL) {
		PERROR("run_server(): malloc for clients");
		free(s->pfds);

		exit(1);
	}

	s->pfds[0].fd     = s->listener;
	s->pfds[0].events = POLLIN;
	s->fd_count       = 1;
}


static int
server_poll(struct server *s)
{
	printf(BOLD_YELLOW(
		"[Server has started ]") "\n"
		"|-> IP Address  : %s\n"
		"|-> Port        : %s\n"
		"|-> Buffer Size : %u bytes\n"
		"`-> Max Clients : %u clients\n\n",

		s->addr, s->port, BUFFER_SIZE, MAX_CLIENTS
	);


	while (is_interrupted == 0) {
		s->poll_ret = poll(s->pfds, s->fd_count, -1);

		if (s->poll_ret < 0) {
			PERROR("server_poll(): poll");

			return -1;
		}

		handle_evs(s);
	}

	return 0;
}


static void
handle_evs(struct server *s)
{
	for (uint16_t i = 0; i < (s->fd_count); i++) {
		if ((s->pfds[i].revents & POLLIN) == 0)
			continue;

		if (s->pfds[i].fd == s->listener) {
			client_acc(s);

			continue;
		}

		client_ev(s, i);
	}
}


static void
client_acc(struct server *s)
{
	int new_fd;
	struct sockaddr_storage addr = {0};
	socklen_t addr_len = sizeof(addr);

	new_fd = accept(s->listener, (struct sockaddr *)&addr, &addr_len);

	if (new_fd < 0) {
		PERROR("client_accept(): accept");

		return;
	}

	/* add new client to pfds array */
	if (add_to_pfds(s, &addr, new_fd) < 0)
		FPERROR("Cannot add new client !!!\n");
}


static void
client_ev(struct server *s, const int index)
{
	struct client *c = &(s->clients[index]);

	if (c->status == DONE) {
		del_from_pfds(s, index);
		
	} else {
		if (c->got_file_prop == true)
			file_io(c);
		else
			get_file_prop(c);
	}
}


static int
add_to_pfds(struct server *s, struct sockaddr_storage *addr, const int new_fd)
{
	struct pollfd *new_pfd;
	struct client *new_cli;

	if (s->fd_count >= MAX_CLIENTS)
		return -1;

	/* Resize pdfs and client array */
	if (s->fd_count == s->fd_size) {
		s->fd_size *= 2; /* Double it */

		new_pfd = realloc(s->pfds, sizeof(struct pollfd) * s->fd_size);
		if (new_pfd == NULL) {
			PERROR("add_to_pfds(): malloc for fds");

			return -1;
		}

		new_cli = realloc(s->clients, sizeof(struct client) * s->fd_size);
		if (new_cli == NULL) {
			PERROR("add_to_pfds(): malloc for clients");

			return -1;
		}

		s->pfds    = new_pfd;
		s->clients = new_cli;
	}


	uint16_t i = s->fd_count;

	/* clearing old values */
	memset(s->pfds +i, 0, sizeof(struct pollfd));
	memset(s->clients +i, 0, sizeof(struct client));

	s->pfds[i].fd     = new_fd;
	s->pfds[i].events = POLLIN;

	s->clients[i].got_file_prop = false;
	s->clients[i].sock_fd       = new_fd;
	s->clients[i].port          = get_port((struct sockaddr *)addr);

	if (get_addr(s->clients[i].addr, (struct sockaddr *)addr) == NULL) {
		PERROR("add_to_pfds(): inet_ntop");

		return -1;
	}


	INFO("New connection from \"%s (%d)\" on socket %d\n",
		s->clients[i].addr, s->clients[i].port, new_fd
	);

	(s->fd_count)++;

	return s->fd_count;
}


static void
del_from_pfds(struct server *s, const int index)
{
	INFO("Closing connection from \"%s (%d)\" on socket %d\n",
		s->clients[index].addr, s->clients[index].port,
		s->clients[index].sock_fd
	);

	s->pfds[index] = s->pfds[s->fd_count -1];

	close(s->clients[index].sock_fd);
	s->clients[index] = s->clients[s->fd_count -1];

	(s->fd_count)--;

	INFO("Done\n");
}


static void
cleanup(struct server *s)
{
	close(s->listener);

	if (s->pfds != NULL) {
		free(s->pfds);
		s->pfds = NULL;
	}

	if (s->clients != NULL) {
		free(s->clients);
		s->clients = NULL;
	}
}


/* ----------- */
int
run_server(int argc, char *argv[])
{
	/*
	 * argv[0] is the bind address
	 * argv[1] is the bind port
	 */

	if (argc != 2) {
		errno = EINVAL;
		PERROR("run_server()");
		print_help();

		return EINVAL;
	}


	int ret;
	struct server srv = {0};

	set_signal(); /* see: ftransfer.c */
	init_server(&srv, argv);

	/* Let's go! */
	ret = server_poll(&srv);

	cleanup(&srv);

	if (ret < 0 || (errno != 0 && errno != EINTR))
		return EXIT_FAILURE;

	INFO("Server has stopped gracefully. :3\n");

	return 0;
}

