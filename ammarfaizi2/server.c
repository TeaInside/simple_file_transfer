/**
 * @author Ammar Faizi <ammarfaizi2@gmail.com>
 * @license MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "data_structure.h"

static void handle_client(int client_fd);
static int server_sokcet(char *bind_addr, uint16_t bind_port);

int main(int argc, char *argv[], char *envp[])
{

  if (argc != 3) {
    printf("Usage: %s <bind_addr> <bind_port>\n", argv[0]);
    return 1;
  }

  return server_sokcet(argv[1], atoi(argv[2]));
}

static int server_sokcet(char *bind_addr, uint16_t bind_port)
{
  int net_fd, client_fd;
  struct sockaddr_in server_addr, client_addr;
  socklen_t rlen = sizeof(struct sockaddr_in);

  /**
   * Prepare server bind address data.
   */
  memset(&server_addr, 0, sizeof(bind_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(bind_port);
  server_addr.sin_addr.s_addr = inet_addr(bind_addr);

  /**
   * Prepare client address data.
   */
  memset(&client_addr, 0, sizeof(client_addr));

  /**
   * Create TCP socket.
   */
  if ((net_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Socket creation failed");
    return 1; /* Don't need to close socket fd since it fails to create. */
  }

  /**
   * Bind socket to address.
   */
  if (bind(net_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("Bind failed");
    goto close_server_sock;
  }

  /**
   * Listen
   */
  if (listen(net_fd, 3) < 0) {
    perror("Listen failed");
    goto close_server_sock;
  }

  printf("Listening on %s:%d...\n", bind_addr, bind_port);

  while (1) {
    /**
     * Accepting client connection.
     */
    client_fd = accept(net_fd, (struct sockaddr *)&client_addr, &rlen);

    printf("Accepting client (%s:%d)...\n",
      inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    if (client_fd < 0) {
      perror("Error accept");
      continue;
    } else {
      handle_client(client_fd);
    }
  }


close_server_sock:
  close(net_fd);
  return 1;
}

#define FILE_INFO_SIZE \
  (size_t)( \
    sizeof(pkt->filename_len) + \
    sizeof(pkt->filename) + \
    sizeof(pkt->file_size) \
  )

static void handle_client(int client_fd)
{
  int file_fd;
  char data_arena[65536], target_file[512];
  ssize_t read_bytes = 0, write_bytes = 0;
  size_t file_read_bytes = 0, read_ok_bytes = 0;
  packet *pkt = (packet *)data_arena;

  printf("Waiting for file...\n");

  /**
   * Receive file info.
   * Make sure file info is completely received before processing file.
   */
  do {

    read_bytes = recv(client_fd, ((char *)pkt) + read_ok_bytes, FILE_INFO_SIZE + BUFFER_SIZE, 0);
    if (read_bytes < 0) {
      perror("Error recv()");
      goto close_client_fd;
    }

    read_ok_bytes += (size_t)read_bytes;

  } while (read_ok_bytes < FILE_INFO_SIZE);

  sprintf(target_file, "uploaded_files/%s", pkt->filename);

  printf("\n\n\n\n");
  file_read_bytes = read_ok_bytes - FILE_INFO_SIZE;
  printf("FILE_INFO_SIZE: %ld\n", FILE_INFO_SIZE);
  printf("read_ok_bytes: %ld\n", read_ok_bytes);
  printf("file_read_bytes: %ld\n", file_read_bytes);
  printf("test min: %ld\n",  read_ok_bytes - FILE_INFO_SIZE);
  printf("test plus: %ld\n", read_ok_bytes + FILE_INFO_SIZE);
  printf("\n\n\n\n");


  printf("=== File Info ===\n");
  printf("Filename: \"%s\"\n", pkt->filename);
  printf("File size: %ld\n", pkt->file_size);
  printf("=====================\n");
  printf("The file will be stored at: \"%s\"\n", target_file);

  /**
   * Create and open file.
   */
  file_fd = open(target_file, O_CREAT | O_WRONLY, S_IRWXU | S_IRGRP | S_IROTH);
  if (file_fd < 0) {
    perror("Error open");
    printf("Cannot create file: \"%s\"\n", target_file);
    goto close_client_fd;
  }

  /**
   * Write some received data.
   */
  write_bytes = write(file_fd, pkt->content, file_read_bytes);
  if (write_bytes < 0) {
    perror("Error write");
    goto close_file_fd;
  }

  printf("Receiving file...\n");

  /**
   * Receiving file...
   */
  uint64_t i = 0;
  while (file_read_bytes < pkt->file_size) {

    /**
     * Reading from socket...
     */
    read_bytes = recv(client_fd, pkt->content, BUFFER_SIZE, 0);
    if (read_bytes < 0) {
      perror("Error recv()");
      goto close_file_fd;
    }

    file_read_bytes += read_bytes;

    if (i % 1000) {
      printf("%ld\n", file_read_bytes);
    }

    i++;

    /**
     * Writing to file...
     */
    write_bytes = write(file_fd, pkt->content, read_bytes);
    if (write_bytes < 0) {
      perror("Error write");
      goto close_file_fd;
    }
  }

  printf("File received completely!\n\n");

close_file_fd:
  close(file_fd);

close_client_fd:
  close(client_fd);
}
