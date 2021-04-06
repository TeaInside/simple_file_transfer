/* @author rLapz <arthurlapz@gmail.com>
 * @license MIT
 *
 * This is part of simple_file_transfer
 * https://github.com/teainside/simple_file_transfer
 *
 * NOTE: true = 0, false = 1
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

/* local include */
#include "data_structure.h"

/* macros */
#define TRUE	0
#define FALSE	1

/* function declarations */
static void file_handler(int *client_fd, int *server_fd);
static int inet_handler(const char *addr, uint16_t port);

/* global variable declarations */
/* anyway, there is no global variable here, yay! */


int
main(int argc, char *argv[], char *envp[])
{
	if (argc < 3 || argc > 3) {
		printf("Usage:\n\t%s [address] [port]\n", argv[0]);
		return EXIT_FAILURE;
	}

	return inet_handler(argv[1], atoi(argv[2]));
}


/* function for handling internet connection */
static int
inet_handler(const char *addr, uint16_t port)
{
	short int is_error			= FALSE;
	int socket_option			= 1;
	int server_fd				= 0;
	int client_fd				= 0;
	struct sockaddr_in server	= {0};
	struct sockaddr_in client	= {0};
	socklen_t client_len		= 0;

	/* Create socket */
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		perror("Create socket");
		is_error = TRUE;
		goto cleanup;
	}

	/* Socket option
	 * Reuse IP address */
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
					&socket_option, sizeof(socket_option)) < 0) {
		perror("Reuse address");
		is_error = TRUE;
		goto cleanup;
	}

	/* TCP configuration */
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(addr);
	server.sin_port = htons(port);

	/* binding server address */
	if (bind(server_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
		perror("Bind socket");
		is_error = TRUE;
		goto cleanup;
	}

	/* listening... */
	if (listen(server_fd, 3) < 0) {
		perror("Listening");
		is_error = TRUE;
		goto cleanup;
	}
	printf("Listening: [%s:%d]...\n", addr, port);

	/* get file properties from client */
	while (1) {
		client_len = sizeof(struct sockaddr_in);
		puts("Waiting for incoming connection...");
		client_fd = accept(server_fd, (struct sockaddr *)&client, &client_len);
		if (client_fd < 0) {
			perror("Accepting connection");
			continue;
		} else {
			puts("Connection accepted");
			file_handler(&client_fd, &server_fd);
		}
	}

cleanup:
	close(server_fd);
	close(client_fd);
	if (is_error == TRUE)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

static void
file_handler(int *client_fd, int *server_fd)
{
	ssize_t recv_bytes		= 0;
	ssize_t recv_ok_bytes	= 0;
	ssize_t write_bytes		= 0;
	size_t total_bytes		= 0;

	short int is_error		= 0;
	int file_desc			= 0;
	char target_file[255]	= {0};
	/* why heap? because I need flexible memory size, though. */
	char *data_arena		= calloc(sizeof(packet), sizeof(packet));
	packet *packet_data		= (packet*)data_arena;
	
	/* get file properties */
	while (recv_ok_bytes < sizeof(packet)) {
		recv_bytes = recv(*client_fd, (char*)packet_data+recv_ok_bytes, BUFFER_SIZE, 0);
		if (recv_bytes < 0) {
			perror("Recv");
			goto cleanup;
		}
		recv_ok_bytes += recv_bytes;
	}

	/* fill packet_data->filename with file properies that received from client */
	snprintf(target_file, sizeof(packet_data->filename),
			"uploaded_files/%s", packet_data->filename);

	/* you know what is these :3 */
	puts("\nFile info: ");
	printf(" -> File name\t\t: %s\n", packet_data->filename);
	printf(" -> File name length\t: %d\n", packet_data->filename_len);
	printf(" -> File size\t\t: %zu byte\n", packet_data->file_size);
	printf(" -> Stored at\t\t: %s\n", target_file);

	/* reallocation memory size */
	/* I know this kinda not safe */
	data_arena = realloc(data_arena, sizeof(packet) + packet_data->file_size);
	memset(data_arena, 0, sizeof(data_arena));
	packet_data = (packet*)data_arena;

	/* Create and open file (overwritten) */
	file_desc = open(target_file, O_WRONLY|O_CREAT|O_TRUNC,
					S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (file_desc < 0)
		perror("Create/open file");

	while (total_bytes < (packet_data->file_size)) {
		/* read bytes data from client */
		recv_bytes = recv(*client_fd, packet_data->content, BUFFER_SIZE, 0);
		if (recv_bytes < 0) {
			perror("Receive file");
			is_error = 1;
			break;
		}

		/* write to desired file/path */
		write_bytes = write(file_desc, packet_data->content, recv_bytes);
		if (write_bytes < 0) {
			perror("Write file");
			is_error = 1;
			break;
		}
		total_bytes += recv_bytes;

		/*
		printf("Recv bytes: %lu\n", total_bytes);
		*/
	}

	if (is_error > 1)
		puts("\nFailed :( \n");
	else
		puts("\nDone, uWu :3\n");

cleanup:
	free(data_arena);
}

