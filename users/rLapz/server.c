
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


struct server {
	int listener;
	packet_t prop;
	struct {
		int fd_count, fd_size, poll_count;
		struct pollfd *pfds;
	} fds;
	struct {
		socklen_t addr_len;
		struct sockaddr_storage remote_addr;
	} net;
};


/* function declarations */
static int   get_listener(const char *addr, const char *port);
static int   add_to_pfds(struct server *s, const int new_fd);
static void  server_poll(struct server *s, const int index);
static const char *get_addr_str(char *dest, struct server *s);
static void  delete_from_pfds(struct server *s, const int index);
static int   get_file_prop(struct server *s, const int index);
static void  file_io(struct server *s, const int index);
static void  clean_up(struct server *s);


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
		fprintf(stderr,
			"server: get_listener(): getaddrinfo: %s\n",
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
	struct pollfd *tmp;

	/* Resizing pfds array */
	if (s->fds.fd_count == s->fds.fd_size) {
		s->fds.fd_size *= 2; /* Double it */

		tmp = realloc(s->fds.pfds, sizeof(struct pollfd) * s->fds.fd_size);
		if (tmp == NULL) {
			perror("server: add_to_pfds(): malloc");
			return -1;
		}

		s->fds.pfds = tmp;
	}

	s->fds.pfds[s->fds.fd_count].fd     = new_fd;
	s->fds.pfds[s->fds.fd_count].events = POLLIN;

	(s->fds.fd_count)++;

	return s->fds.fd_count;
}


static void
server_poll(struct server *s, const int index)
{
	int new_fd;
	char addr_str[INET6_ADDRSTRLEN];

	if (s->fds.pfds[index].fd == s->listener) {
		s->net.addr_len = sizeof(s->net.remote_addr);
		new_fd = accept(s->listener,
				(struct sockaddr *)&(s->net.remote_addr),
				&(s->net.addr_len));

		if (new_fd < 0) {
			perror("server: server_poll(): accept");
			return;
		}

		printf("server: new connection from %s on socket %d\n\n",
				get_addr_str(addr_str, s), new_fd);

		if (add_to_pfds(s, new_fd) < 0) {
			clean_up(s);
			die("server: server_poll()");
		}

		return;
	}

	/* TODO: Multithreading support (file I/O) */

	file_io(s, index);

	close(s->fds.pfds[index].fd);

	printf("server: client on socket %d has been closed\n",
			s->fds.pfds[index].fd
	);

	delete_from_pfds(s, index);
}


static const char *
get_addr_str(char *dest, struct server *s)
{
	return  inet_ntop(s->net.remote_addr.ss_family,
			get_in_addr((struct sockaddr *)&(s->net.remote_addr)),
			dest, INET6_ADDRSTRLEN
		);
}


static void
delete_from_pfds(struct server *s, const int index)
{
	s->fds.pfds[index] = s->fds.pfds[s->fds.fd_count -1];
	(s->fds.fd_count)--;
}


static void
clean_up(struct server *s)
{
	close(s->listener);

	if (s->fds.pfds != NULL) {
		free(s->fds.pfds);
		s->fds.pfds = NULL;
	}
}


static int
get_file_prop(struct server *s, const int index)
{
	char   *raw_prop;
	char    addr_str[INET6_ADDRSTRLEN];
	size_t  t_bytes = 0;
	size_t  p_size  = sizeof(packet_t);
	ssize_t s_bytes;

	memset(&(s->prop), 0, p_size);
	raw_prop = (char *)&(s->prop);

	while (t_bytes < p_size && is_interrupted == 0) {
		s_bytes = recv(s->fds.pfds[index].fd,
				raw_prop + t_bytes, p_size - t_bytes, 0);

		if (s_bytes < 0) {
			fprintf(stderr,
				"server: file_io(): recv: %s on socket %d\n",
				get_addr_str(addr_str, s), s->fds.pfds[index].fd
			);

			return -1;
		}

		if (s_bytes == 0) {
			fprintf(stderr,
				"server: file_io(): client %s on socket %d was disconnected\n",
				get_addr_str(addr_str, s), s->fds.pfds[index].fd
			);

			return -1;
		}

		t_bytes += (size_t)s_bytes;
	}
 
	if (file_check(&(s->prop)) < 0) {
		fprintf(stderr, "server: get_file_prop(): Invalid file name\n");
		return -1;
	}

	if (t_bytes != p_size) {
		fprintf(stderr, 
			"File properties is corrupted or it's size did not match!\n"
		);
		return -1;
	}

	printf(BOLD_WHITE("File properties [%s] on socket %d") "\n",
				get_addr_str(addr_str, s), s->fds.pfds[index].fd);
	printf("|-> File name : %s (%u)\n", s->prop.file_name,
				s->prop.file_name_len);
	printf("`-> File size : %" PRIu64 " bytes\n", be64toh(s->prop.file_size));

	return 0;
}

static void
file_io(struct server *s, const int index)
{
	FILE    *file_d;
	ssize_t  rv_bytes;
	uint64_t b_total = 0;
	uint64_t p_size;

	char addr_str[INET6_ADDRSTRLEN];
	char buffer[BUFFER_SIZE];
	char full_path[sizeof(DEST_DIR) + sizeof(s->prop.file_name)];

	if (get_file_prop(s, index) < 0)
		return;

	if (snprintf(full_path, sizeof(full_path), "%s/%s",
			DEST_DIR, s->prop.file_name) < 0) {

		perror("server: file_io(): set full path");
		return;
	}

	if ((file_d = fopen(full_path, "w")) == NULL) {
		perror("server: file_io(): fopen");
		return;
	}

	p_size = be64toh(s->prop.file_size);

	puts("\nWriting...");
	while (b_total < p_size && is_interrupted == 0) {
		rv_bytes = recv(s->fds.pfds[index].fd, buffer, sizeof(buffer), 0);

		if (rv_bytes < 0) {
			fprintf(stderr,
				"server: file_io(): recv: %s on socket %d\n",
				get_addr_str(addr_str, s),
				s->fds.pfds[index].fd
			);

			break;
		}

		if (rv_bytes == 0) {
			fprintf(stderr,
				"server: file_io(): client %s on socket %d was disconnected\n",
				get_addr_str(addr_str, s),
				s->fds.pfds[index].fd
			);
			break;
		}

		b_total += (uint64_t)fwrite(buffer, 1, (size_t)rv_bytes, file_d);

		if (ferror(file_d) != 0) {
			perror("server: file_io(): fwrite");
			break;
		}
	}

	fflush(file_d);
	puts("Buffer flushed");

	fclose(file_d);

	if (b_total != p_size) {
		fprintf(stderr, 
			"File \"%s\" is corrupted or file size did not match!\n",
			s->prop.file_name
		);
		return;
	}

	puts("Done!\n");
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

	struct server srv    = {0};

	set_signal(); /* see: ftransfer.c */

	srv.fds.fd_size = 5;
	srv.fds.pfds    = calloc(sizeof(struct pollfd),
				sizeof(struct pollfd) * srv.fds.fd_size);

	if (srv.fds.pfds == NULL) {
		perror("server: run_server(): malloc");
		return EXIT_FAILURE;
	}

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
		srv.fds.poll_count = poll(srv.fds.pfds, srv.fds.fd_count, -1);
		if (srv.fds.poll_count == -1)
			perror("server: server_poll(): poll");

		for (int i = 0; i < srv.fds.fd_count; i++) {
			if (srv.fds.pfds[i].revents & POLLIN)
				server_poll(&srv, i);
		}
	}

	clean_up(&srv);

	puts("server: Stopped");

	if (errno != 0 && errno != EINTR)
		return EXIT_FAILURE;

	puts("server: server has been stopped gracefully. :3");

	return EXIT_SUCCESS;
}

