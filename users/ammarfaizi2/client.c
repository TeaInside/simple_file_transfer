// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer client
 *
 * Copyright (C) 2021  Ammar Faizi <ammarfaizi2@gmail.com>
 */

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE
#endif

#include <poll.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <libgen.h>
#include <stdbool.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "ftransfer.h"

#define DEBUG			(1)
#define SEND_BUFFER_SIZE	(0x4000u)

#if DEBUG
#  define printf_dbg(...) printf(__VA_ARGS__)
#else
#  define printf_dbg(...)
#endif


union uni_pkt {
	packet_t	packet;
	char		raw_buf[SEND_BUFFER_SIZE];
};

static_assert(SEND_BUFFER_SIZE >= sizeof(packet_t), "Bad SEND_BUFFER_SIZE");

struct client_state {
	bool		stop_el;
	int		tcp_fd;
	const char	*target_file;
	FILE		*handle;
	union uni_pkt	pktbuf;
};


static struct client_state *g_state;


static void handle_interrupt(int sig)
{
	g_state->stop_el = true;
	putchar('\n');
	(void)sig;
}


static int init_state(struct client_state *state)
{
	state->stop_el   = false;
	state->tcp_fd	 = -1;
	state->handle    = NULL;
	return 0;
}


static int open_target_file(struct client_state *state)
{
	FILE *handle;

	handle = fopen(state->target_file, "rb");
	if (handle == NULL) {
		int err = errno;
		printf("Error: fopen(\"%s\"): %s\n", state->target_file,
		       strerror(err));
		return -err;
	}

	state->handle = handle;
	return 0;
}


static int socket_setup(int tcp_fd)
{
	int y;
	int err;
	int retval;
	const char *lv, *on; /* level and optname */
	socklen_t len = sizeof(y);
	const void *py = (const void *)&y;

	y = 1;
	retval = setsockopt(tcp_fd, IPPROTO_TCP, TCP_NODELAY, py, len);
	if (retval < 0) {
		lv = "IPPROTO_TCP";
		on = "TCP_NODELAY";
		goto out_err;
	}

	return 0;
out_err:
	err = errno;
	printf("Error: setsockopt(tcp_fd, %s, %s): %s\n", lv, on, strerror(err));
	return -err;
}


static int init_socket(const char *server_addr, uint16_t server_port,
		       struct client_state *state)
{
	int ret;
	int tcp_fd;
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);

	tcp_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (tcp_fd < 0) {
		ret = errno;
		printf("Error: socket(): %s\n", strerror(ret));
		return -ret;
	}

	ret = socket_setup(tcp_fd);
	if (ret)
		return ret;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(server_port);
	addr.sin_addr.s_addr = inet_addr(server_addr);

again:
	printf("Connecting to %s:%u...\n", server_addr, server_port);
	ret = connect(tcp_fd, (struct sockaddr *)&addr, addr_len);
	if (ret) {
		ret = errno;
		if (ret == EINPROGRESS || ret == EALREADY || ret == EAGAIN) {
			usleep(10000);
			goto again;
		}

		printf("Error: connect(): %s\n", strerror(ret));
		ret = -ret;
		goto out;
	}

	printf("Connection established!\n");
	state->tcp_fd = tcp_fd;
out:
	if (ret)
		close(tcp_fd);
	return ret;
}


static uint64_t get_file_size(FILE *handle)
{
	int err;
	uint64_t ret;
	err = fseek(handle, 0L, SEEK_END);
	if (err) {
		err = errno;
		printf("Error: fseek(): %s\n", strerror(err));
		errno = err;
		return 0;
	}
	ret = (uint64_t)ftell(handle);
	rewind(handle);
	return ret;
}


static int send_target_file(struct client_state *state)
{
	size_t send_size;
	uint64_t file_size;
	const char *file_base_name;
	int tcp_fd = state->tcp_fd;
	FILE *handle = state->handle;
	packet_t *pkt = &state->pktbuf.packet;
	char file_name[0x1000];
	size_t fread_ret;
	int err;
	struct pollfd fds[1];

	snprintf(file_name, sizeof(file_name), "%s", state->target_file);
	file_base_name = basename(file_name);

	errno = 0;
	file_size= get_file_size(handle);
	if (file_size == 0 && errno != 0)
		return -errno;


	printf("=================================\n");
	printf("File name: %s\n", file_base_name);
	printf("File size: %" PRIu64 "\n", file_size);
	printf("=================================\n");

	pkt->file_size = htobe64(file_size);
	strncpy(pkt->file_name, file_base_name, sizeof(pkt->file_name));
	pkt->file_name[sizeof(pkt->file_name) - 1] = '\0';
	pkt->file_name_len = (uint8_t)strlen(pkt->file_name);

	printf("Sending file to server...\n");
	send_size = sizeof(*pkt);


	fds[0].fd = tcp_fd;
	fds[0].events = POLLOUT;
	fds[0].revents = 0;

	do {
		ssize_t send_ret;
		char *raw_buf = state->pktbuf.raw_buf;

		fread_ret = fread(state->pktbuf.raw_buf + send_size,
				  sizeof(char),
				  SEND_BUFFER_SIZE - send_size,
				  handle);

		send_size += fread_ret;
		file_size -= fread_ret;

	exec_send:
		if (state->stop_el) {
			printf("Stopping event loop...\n");
			break;
		}

		send_ret = send(tcp_fd, raw_buf, send_size, 0);
		if (send_ret < 0) {
			err = errno;
			if (err == EAGAIN) {
				int ret;
				/*
				 *
				 * Oops, the network buffer is full
				 * because this loop is too fast
				 * compared to the network speed.
				 *
				 * We must wait until the network buffer
				 * is flushed and is ready for writing.
				 *
				 * Let's sleep on poll() and wait for
				 * the kernel to wake us up when writing
				 * buffer is ready again.
				 *
				 */
				printf("Sleeping on poll()...\n");
			exec_poll:
				ret = poll(fds, 1, 1000);
				if (state->stop_el)
					goto exec_send;
				if (ret < 1)
					goto exec_poll;

				goto exec_send;
			}
			printf("Error: send(): %s\n", strerror(err));
			return -err;
		}

		printf_dbg("send() %zu bytes to the server\n", send_ret);
		if ((size_t)send_ret < send_size) {
			/*
			 *
			 * Only partial bytes are sent, must
			 * resend the pending buffer.
			 *
			 */
			send_size -= (size_t)send_ret;
			memmove(raw_buf, raw_buf + (size_t)send_ret, send_size);
			printf_dbg("Partial bytes are sent, resend pending buffer...\n");
			goto exec_send;
		}
		send_size = 0;
	
	} while (file_size > 0);

	printf("File sent completely!\n");
	return 0;
}


static void destroy_state(struct client_state *state)
{
	int tcp_fd = state->tcp_fd;
	FILE *handle = state->handle;

	if (tcp_fd != -1) {
		printf("Closing tcp_fd (%d)...\n", tcp_fd);
		close(tcp_fd);
	}

	if (handle != NULL) {
		fclose(handle);
	}
}


static int internal_run_client(char *argv[])
{
	int ret;
	struct client_state *state;

	state = malloc(sizeof(*state));
	if (state == NULL) {
		printf("Error: malloc(): %s", strerror(ENOMEM));
		return -ENOMEM;
	}
	memset(state, 0, sizeof(*state));
	g_state = state;

	signal(SIGINT, handle_interrupt);
	signal(SIGTERM, handle_interrupt);
	signal(SIGHUP, handle_interrupt);
	signal(SIGPIPE, SIG_IGN);

	ret = init_state(state);
	if (ret)
		goto out;

	state->target_file = argv[2];
	ret = open_target_file(state);
	if (ret)
		goto out;

	ret = init_socket(argv[0], (uint16_t)atoi(argv[1]), state);
	if (ret)
		goto out;

	ret = send_target_file(state);
out:
	destroy_state(state);
	free(state);
	return ret;
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

	return -internal_run_client(argv);
}
