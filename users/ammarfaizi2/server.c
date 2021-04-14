// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file transfer server
 *
 * Copyright (C) 2021  Ammar Faizi <ammarfaizi2@gmail.com>
 */

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "ftransfer.h"


#define DEBUG			(1)
#define MAX_CLIENTS		(100u)
#define EPOLL_MAP_SIZE		(0xffffu)
#define EPOLL_MAP_TO_NOP	(0x0u)
#define EPOLL_MAP_TO_TCP	(0x1u)
#define EPOLL_MAP_SHIFT		(0x2u)
#define EPOLL_INPUT_EVT		(EPOLLIN | EPOLLPRI)
#define RECV_BUFFER_SIZE	(0x4000u)

/* Macros for printing  */
#define W_IP(CHAN) ((CHAN)->src_ip), ((CHAN)->src_port)
#define W_IU(CHAN) W_IP(CHAN)
#define PRWIU "%s:%u"

#ifndef INET_ADDRSTRLEN
#  define IPV4_L (sizeof("xxx.xxx.xxx.xxx"))
#else
#  define IPV4_L (INET_ADDRSTRLEN)
#endif

#if DEBUG
#  define printf_dbg(...) printf(__VA_ARGS__)
#else
#  define printf_dbg(...)
#endif


union uni_pkt {
	packet_t	packet;
	char		raw_buf[RECV_BUFFER_SIZE];
};

static_assert(RECV_BUFFER_SIZE >= sizeof(packet_t), "Bad RECV_BUFFER_SIZE");

struct client_channel {
	bool		is_used;	/* Is this channel used?              */
	bool		got_file_info;	/* Have we received file info?        */
	int		cli_fd;		/* Client file descriptor             */
	union uni_pkt	pktbuf;		/* Packet buffer                      */
	size_t		recv_s;		/* How many active bytes in packet?   */
	uint16_t	arr_idx;	/* Index in the channel array         */
	char		src_ip[IPV4_L];	/* Human readable src IPv4            */
	uint16_t	src_port;	/* Human readable src port            */
	uint64_t	recv_file_len;	/* Received file bytes                */
	char		filename[256];	/* Filename                           */
	FILE		*handle;	/* File handle                        */
};

struct server_state {
	bool			stop_el;	/* Stop the event loop?       */
	int			tcp_fd;		/* Main TCP file descriptor   */
	int			epoll_fd;	/* Epoll file descriptor      */
	struct client_channel	*chans;		/* Channel array              */
	uint16_t		*epoll_map;	/* Mapping for O(1) retrieval */
	uint16_t		av_client;	/* How many unused array slot?*/
	const char		*storage_path;	/* Path to save uploaded files*/
};


static struct server_state *g_state;


static void handle_interrupt(int sig)
{
	g_state->stop_el = true;
	putchar('\n');
	(void)sig;
}


static inline void *calloc_wrp(size_t nmemb, size_t size)
{
	void *ret = calloc(nmemb, size);
	if (ret == NULL) {
		printf("Error: calloc(): %s\n", strerror(ENOMEM));
		errno = ENOMEM;
	}
	return ret;
}


static inline void reset_client(struct client_channel *chan, uint16_t idx)
{
	chan->is_used       = false;
	chan->got_file_info = false;
	chan->cli_fd        = -1;
	chan->recv_s        = 0;
	chan->arr_idx       = idx;
	chan->recv_file_len = 0;
	chan->handle        = NULL;
}


static int init_channels(struct server_state *state)
{
	struct client_channel *chans;

	chans = calloc_wrp(MAX_CLIENTS, sizeof(*chans));
	if (chans == NULL)
		return -ENOMEM;

	for (uint16_t i = 0; i < MAX_CLIENTS; i++)
		reset_client(&chans[i], i);

	state->chans = chans;
	return 0;
}


static int init_epoll_map(struct server_state *state)
{
	uint16_t *epoll_map;

	epoll_map = calloc_wrp(MAX_CLIENTS, sizeof(*epoll_map));
	if (epoll_map == NULL)
		return -ENOMEM;

	for (uint16_t i = 0; i < MAX_CLIENTS; i++)
		epoll_map[i] = EPOLL_MAP_TO_NOP;

	state->epoll_map = epoll_map;
	return 0;
}


static int init_state(struct server_state *state)
{
	int ret;
	state->stop_el   = false;
	state->tcp_fd	 = -1;
	state->epoll_fd	 = -1;
	state->av_client = MAX_CLIENTS;

	ret = init_channels(state);
	if (ret)
		return ret;

	ret = init_epoll_map(state);
	if (ret)
		return ret;

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
	retval = setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, py, len);
	if (retval < 0) {
		lv = "SOL_SOCKET";
		on = "SO_REUSEADDR";
		goto out_err;
	}

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


static int epoll_add(int epoll_fd, int fd, uint32_t events)
{
	int err;
	struct epoll_event event;

	/* Shut the valgrind up! */
	memset(&event, 0, sizeof(struct epoll_event));

	event.events  = events;
	event.data.fd = fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
		err = errno;
		printf("Error: epoll_ctl(EPOLL_CTL_ADD): %s\n", strerror(err));
		return -err;
	}
	return 0;
}


static int epoll_delete(int epl_fd, int fd)
{
	int err;

	if (epoll_ctl(epl_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
		err = errno;
		printf("Error: epoll_ctl(EPOLL_CTL_DEL): %s\n", strerror(err));
		return -err;
	}
	return 0;
}


static int init_epoll(struct server_state *state)
{
	int err;
	int epoll_fd;

	epoll_fd = epoll_create(255);
	if (epoll_fd < 0) {
		err = errno;
		printf("Error: epoll_create(): %s\n", strerror(err));
		return -err;
	}

	state->epoll_fd = epoll_fd;
	return 0;
}


static int init_socket(const char *bind_addr, uint16_t bind_port,
		       struct server_state *state)
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
	addr.sin_port = htons(bind_port);
	addr.sin_addr.s_addr = inet_addr(bind_addr);

	ret = bind(tcp_fd, (struct sockaddr *)&addr, addr_len);
	if (ret) {
		ret = errno;
		printf("Error: bind(): %s\n", strerror(ret));
		ret = -ret;
		goto out;
	}

	ret = listen(tcp_fd, 30);
	if (ret) {
		ret = errno;
		printf("Error: listen(): %s\n", strerror(ret));
		ret = -ret;
		goto out;
	}

	ret = epoll_add(state->epoll_fd, tcp_fd, EPOLL_INPUT_EVT);
	if (ret)
		goto out;

	state->tcp_fd = tcp_fd;
	state->epoll_map[tcp_fd] = EPOLL_MAP_TO_TCP;
	printf("Listening on %s:%u...\n", bind_addr, bind_port);
out:
	if (ret)
		close(tcp_fd);
	return ret;
}


static const char *convert_addr_ntop(struct sockaddr_in *addr, char *src_ip_buf)
{
	int err;
	const char *ret;
	in_addr_t saddr = addr->sin_addr.s_addr;

	ret = inet_ntop(AF_INET, &saddr, src_ip_buf, IPV4_L);
	if (ret == NULL) {
		err = errno;
		err = err ? err : EINVAL;
		printf("Error: inet_ntop(): %s\n", strerror(err));
		return NULL;
	}

	return ret;
}


static int resolve_src_info(struct sockaddr_in *addr, char *src_ip,
			    uint16_t *src_port)
{
	*src_port = ntohs(addr->sin_port);
	if (convert_addr_ntop(addr, src_ip) == NULL)
		return -EINVAL;

	return 0;
}


static int run_acceptor(int tcp_fd, struct server_state *state)
{
	int ret;
	int cli_fd;
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	struct client_channel *chans, *chan;

	uint16_t i;
	uint16_t src_port;
	char src_ip[IPV4_L + 1];

	memset(&addr, 0, sizeof(addr));
	cli_fd = accept(tcp_fd, (struct sockaddr *)&addr, &addr_len);
	if (cli_fd == -1) {
		ret = errno;
		if (ret == EAGAIN)
			return 0;

		printf("Error: accept(): %s\n", strerror(ret));
		return -ret;
	}


	if (addr_len > sizeof(addr)) {
		printf("Error: accept(): %s\n", strerror(EOVERFLOW));
		ret = -EOVERFLOW;
		goto out;
	}


	ret = resolve_src_info(&addr, src_ip, &src_port);
	if (ret)
		goto out;


	if ((uint16_t)cli_fd > (EPOLL_MAP_SIZE - EPOLL_MAP_SHIFT)) {
		printf("Error: accept() yielded too big file descriptor, "
		       "max_allowed: %u, cli_fd: %d",
		       (EPOLL_MAP_SIZE - EPOLL_MAP_SHIFT), cli_fd);
		ret = -EOVERFLOW;
		goto out;
	}

	/*
	 * Find unused client slot in the array
	 *
	 * TODO: Implement stack to retrieve unused slot in O(1).
	 *
	 */
	chans = state->chans;
	for (i = 0; i < MAX_CLIENTS; i++) {
		chan = &chans[i];
		if (!chan->is_used)
			goto got_unused;
	}

	printf("Error: Cannot accept connection from %s:%u (channel is full)\n",
	       src_ip, src_port);
	ret = -EAGAIN;
	goto out;


got_unused:
	chan->cli_fd   = cli_fd;
	chan->is_used  = true;
	chan->recv_s   = 0;
	chan->src_port = src_port;
	strncpy(chan->src_ip, src_ip, sizeof(chan->src_ip));
	state->av_client--;
	printf("Accepted connection from " PRWIU "\n", W_IU(chan));
	epoll_add(state->epoll_fd, cli_fd, EPOLL_INPUT_EVT);
	state->epoll_map[cli_fd] = i + EPOLL_MAP_SHIFT;
out:
	if (ret)
		close(cli_fd);
	return ret;
}


static int handle_tcp_event(int tcp_fd, struct server_state *state,
			    uint32_t revents)
{
	const uint32_t err_mask = EPOLLERR | EPOLLHUP;

	if (revents & err_mask) {
		printf("Error: TCP event error");
		return -ENOTCONN;
	}

	return run_acceptor(tcp_fd, state);
}


static int open_client_file_handle(struct server_state *state,
				   struct client_channel *chan,
				   const char *filename)
{
	int err;
	FILE *handle;
	char target_file[1024];

	snprintf(target_file, sizeof(target_file), "%s/%s", state->storage_path,
		 filename);

	handle = fopen(target_file, "wb");
	if (handle == NULL) {
		err = errno;
		printf("Cannot create file: %s: %s\n", target_file,
		       strerror(err));
		return -err;
	}
	setvbuf(handle, NULL, _IOFBF, RECV_BUFFER_SIZE * 2u);

	/* TODO: Print file info */
	chan->handle = handle;
	return 0;
}


static int handle_file_info(struct server_state *state,
			    struct client_channel *chan, size_t recv_s)
{
	int ret = 0;
	uint64_t file_size;
	uint64_t total_expected;
	packet_t *pkt = &chan->pktbuf.packet;

	if (recv_s < sizeof(*pkt)) {
		/*
		 * We haven't received the file info, must
		 * wait a bit longer.
		 *
		 * Bail out!
		 */
		goto out;
	}


	/*
	 * Now, it is safe to read the packet info
	 */
	file_size = pkt->file_size;

	total_expected = sizeof(*pkt) + file_size;
	if (recv_s > total_expected) {
		/*
		 * Expected total bytes sent by client
		 * is `total_expected`. If we receive
		 * more than that at this point, then
		 * client has done something totally
		 * wrong!
		 */
		printf("Client " PRWIU " sends invalid packet\n", W_IU(chan));
		ret = -EINVAL;
		goto out;
	}


	memcpy(chan->filename, pkt->filename, pkt->filename_len);

	/*
	 * Ensure null terminator for safety.
	 */
	chan->filename[pkt->filename_len] = '\0';
	chan->filename[sizeof(chan->filename) - 1] = '\0';
	chan->got_file_info = true;

	ret = open_client_file_handle(state, chan, chan->filename);
	if (ret)
		goto out;

	if (recv_s > sizeof(*pkt)) {
		/*
		 * Partial bytes of the file has
		 * arrived together with the file info.
		 *
		 * Must memmove to the front before
		 * we run out of buffer!
		 */
		recv_s -= sizeof(*pkt);
		memmove(chan->pktbuf.raw_buf,
			chan->pktbuf.raw_buf + sizeof(*pkt), recv_s);

		chan->recv_s = recv_s;
		ret = -EAGAIN;
		goto out;
	}
out:
	return ret;
}


static int handle_file_content(struct server_state *state,
			       struct client_channel *chan, size_t recv_s)
{

}


static int handle_client_data(struct server_state *state,
			      struct client_channel *chan, size_t recv_s)
{
	int ret = 0;
	chan->recv_s = recv_s;
again:
	if (chan->got_file_info) {
		ret = handle_file_content(state, chan, recv_s);
	} else {
		ret = handle_file_info(state, chan, recv_s);
		if (ret == -EAGAIN) {
			recv_s = chan->recv_s;
			goto again;
		}
	}

	return ret;
}


static int handle_client_event(int cli_fd, struct server_state *state,
			       struct client_channel *chan, uint32_t revents)
{
	int err;
	char *recv_buf;
	size_t recv_s;
	size_t recv_len;
	ssize_t recv_ret;
	const uint32_t err_mask = EPOLLERR | EPOLLHUP;

	if ((revents & err_mask) || (chan->cli_fd == -1))
		goto out_close;

	recv_s   = chan->recv_s;
	recv_buf = chan->pktbuf.raw_buf;
	recv_len = sizeof(chan->pktbuf.raw_buf) - recv_s;
	recv_ret = recv(cli_fd, recv_buf, recv_len, 0);
	if (recv_ret == 0)
		goto out_close;

	if (recv_ret < 0) {
		err = errno;
		if (err == EAGAIN)
			return 0;
		printf("Error: recv(): %s\n", strerror(err));
		goto out_close;
	}

	printf_dbg("recv() %zd bytes from " PRWIU "\n", recv_ret, W_IU(chan));
	recv_s += (size_t)recv_ret;
	if (handle_client_data(state, chan, recv_s))
		goto out_close;

	return 0;
out_close:
	printf("Closing connection from " PRWIU "...\n", W_IU(chan));
	if (chan->handle != NULL) {
		fflush(chan->handle);
		fclose(chan->handle);
	}
	state->av_client++;
	state->epoll_map[cli_fd] = EPOLL_MAP_TO_NOP;
	epoll_delete(state->epoll_fd, cli_fd);
	reset_client(chan, chan->arr_idx);
	close(cli_fd);
	return 0;
}


static int handle_event(struct server_state *state, struct epoll_event *event)
{
	int fd = event->data.fd;
	uint16_t map_to;
	uint16_t *epoll_map = state->epoll_map;
	uint32_t revents = event->events;

	map_to = epoll_map[fd];
	if (map_to == EPOLL_MAP_TO_NOP) {
		printf("Bug: epoll_map[%d] contains EPOLL_MAP_TO_NOP\n", fd);
		abort();
		return -EFAULT;
	}


	if (map_to == EPOLL_MAP_TO_TCP)
		/*
		 * A client is connecting to us.
		 */
		return handle_tcp_event(fd, state, revents);


	/*
	 * A client calls send(), let's recv() it.
	 */
	map_to -= EPOLL_MAP_SHIFT;
	return handle_client_event(fd, state, &state->chans[map_to], revents);
}


static int handle_events(struct server_state *state, struct epoll_event *events,
			 int epoll_ret)
{
	int ret;
	for (int i = 0; i < epoll_ret; i++) {
		ret = handle_event(state, &events[i]);
		if (ret)
			return ret;
	}

	return 0;
}


static int run_event_loop(struct server_state *state)
{
	int err;
	int ret = 0;
	int epoll_ret;
	int timeout = 1000; /* in milliseconds */
	int maxevents = 32;
	int epoll_fd = state->epoll_fd;
	struct epoll_event events[32];

	while (!state->stop_el) {

		epoll_ret = epoll_wait(epoll_fd, events, maxevents, timeout);
		if (epoll_ret == 0) {
			/* 
			 * Epoll reached timeout
			 *
			 * TODO: Client timeout monitoring.
			 */
			continue;
		}

		if (epoll_ret < 0) {
			err = errno;
			if (err == EINTR) {
				printf("Interrupted!\n");
				continue;
			}

			ret = -err;
			printf("Error: epoll_wait(): %s\n", strerror(err));
			break;
		}

		ret = handle_events(state, events, epoll_ret);
		if (ret) {
			if (ret == -EAGAIN)
				continue;
			if (ret == -ECANCELED)
				continue;
			break;
		}
	}

	return ret;
}


static void destroy_state(struct server_state *state)
{
	int tcp_fd = state->tcp_fd;
	int epoll_fd = state->epoll_fd;

	if (tcp_fd != -1) {
		printf("Closing tcp_fd (%d)...\n", tcp_fd);
		close(tcp_fd);
	}

	if (epoll_fd != -1) {
		printf("Closing epoll_fd (%d)...\n", epoll_fd);
		close(epoll_fd);
	}

	free(state->epoll_map);
	free(state->chans);
}


static int internal_run_client(char *argv[])
{
	int ret;
	struct server_state *state;

	state = malloc(sizeof(*state));
	if (state == NULL) {
		printf("Error: malloc(): %s", strerror(ENOMEM));
		return -ENOMEM;
	}
	memset(state, 0, sizeof(*state));
	g_state = state;

	state->storage_path = "uploaded_files";

	signal(SIGINT, handle_interrupt);
	signal(SIGTERM, handle_interrupt);
	signal(SIGHUP, handle_interrupt);
	signal(SIGPIPE, SIG_IGN);

	ret = init_state(state);
	if (ret)
		goto out;

	ret = init_epoll(state);
	if (ret)
		goto out;

	ret = init_socket(argv[0], (uint16_t)atoi(argv[1]), state);
	if (ret)
		goto out;

	ret = run_event_loop(state);
out:
	destroy_state(state);
	free(state);
	return ret;
}


int run_server(int argc, char *argv[])
{
	/*
	 * argv[0] is bind address
	 * argv[1] is bind port
	 */

	if (argc != 2) {
		printf("Error: Invalid argument on run_server\n");
		print_help();
		return EINVAL;
	}

	return -internal_run_client(argv);
}
