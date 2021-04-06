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
#include <sys/stat.h>

/* local include */
#include "data_structure.h"

/* macros */
#define TRUE	0
#define FALSE	1

/* function declarations */
static int inet_handler(const char* address,
		uint16_t port, const char* filename);
static char * get_basename(const char *path);

/* global variable declarations */
/* anyway, there is no global variable here, yay! */


int
main(int argc, char *argv[], char *envp[])
{
	if (argc < 4) {
		printf("Usage:\n\t%s [address] [port] [file_target]\n", argv[0]);
		return EXIT_FAILURE;
	}

	return inet_handler(argv[1], atoi(argv[2]), argv[3]);
}

/* function for handling internet connection
 * and file i/o operations.
 * sadly, not written into separated function, yet!
 */
static int
inet_handler(const char* address,
		uint16_t port, const char* filename)
{
	struct stat s_file			= {0};
	struct sockaddr_in server	= {0};
	short int is_error			= FALSE;
	int client_fd				= 0;
	int file_fd					= 0;
	ssize_t read_bytes			= 0;
	ssize_t send_bytes			= 0;
	size_t sent_bytes			= 0;
	/* why heap? because I need flexible memory size, though. */
	char *data_arena			= calloc(sizeof(packet), sizeof(packet));
	packet *packet_data			= (packet*)data_arena;
	char *basename				= NULL;

	/* file checking */
	if (stat(filename, &s_file) < 0) {
		perror(filename);
		is_error = TRUE;
		goto cleanup;
	}

	/*
	 * set file properties
	 */

	/* get basename of a file */
	basename = get_basename(filename);
	if (strlen(basename) == 0) {
		fprintf(stderr, "File/path invalid\n");
		is_error = TRUE;
		goto cleanup;
	}

	strncpy(packet_data->filename, basename, sizeof(packet_data->filename));
	packet_data->filename_len = strlen(basename);
	packet_data->file_size = s_file.st_size;

	/* print file properties */
	puts("\nFile info: ");
	printf(" -> File name\t\t: %s\n", packet_data->filename);
	printf(" -> File name length:\t: %d\n", packet_data->filename_len);
	printf(" -> File size\t\t: %zu byte\n", packet_data->file_size);
	printf(" -> Send to\t\t: %s:%d\n", address, port);

	/* Create socket */
	client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd < 0) {
		perror("Create socket");
		is_error = TRUE;
		goto cleanup;
	}

	/* TCP configuration */
	server.sin_addr.s_addr = inet_addr(address);
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	/* let's connect to server, yay! */
	if (connect(client_fd, (struct sockaddr*)&server,
				sizeof(server)) < 0) {
		perror("Connect to server");
		is_error = TRUE;
		goto cleanup;
	}

	/* send file properties */
	send_bytes = send(client_fd, packet_data, sizeof(packet), 0);
	if (send_bytes < 0) {
		perror("Send data");
		is_error = TRUE;
		goto cleanup;
	}

	/* Reallocation memory size*/
	data_arena = realloc(data_arena, sizeof(packet) + packet_data->file_size);
	packet_data = (packet*)data_arena;

	puts("\nSending file...");

	/* openning file target */
	file_fd = open(filename, O_RDONLY, 0);
	if (file_fd < 0) {
		perror("Open file");
		is_error = TRUE;
		goto cleanup;
	}

	/* send file to server */
	while (sent_bytes < (packet_data->file_size)) {
		read_bytes = read(file_fd, packet_data->content, READ_FILE_BUF);
		if (read_bytes < 0) {
			perror("Read file");
			is_error = TRUE;
			break;
		}
		send_bytes = send(client_fd, packet_data->content, read_bytes, 0);
		if (sent_bytes < 0) {
			perror("Send file");
			is_error = TRUE;
			break;
		}
		sent_bytes += read_bytes;
		/*
		printf("Sent bytes: %lu\n", sent_bytes);
		*/
	}
	puts("\nDone, uWu :3\n");

cleanup:
	free(basename);
	free(data_arena);
	if (client_fd > 0)
		close(client_fd);
	if (file_fd > 0)
		close(file_fd);
	if (is_error == TRUE)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

/* function for generate basename of a file */
/* because we need 'real' filename not fullpath */
static char *
get_basename(const char *path)
{
	char *str = strrchr(path, '/');
	if (str == NULL)
		return strdup(path);

	return strdup(str +1);
}
