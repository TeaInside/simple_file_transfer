
/**
 * @author Ammar Faizi <ammarfaizi2@gmail.com>
 * @license MIT
 *
 * Simple socket server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "data_structure.h"

int client_socket(char *server_ip, uint16_t server_port);

int main(int argc, char *argv[])
{
  
  if (argc != 3) {
    printf("Invalid argument!\n");
    printf("Usage: %s <bind_ip> <bind_port>\n", argv[0]);
    return 1;
  }

  return client_socket(argv[1], atoi(argv[2]));
}

int client_socket(char *server_ip, uint16_t server_port)
{
  uint64_t sent_file_size = 0;
  ssize_t read_bytes, write_bytes;
  int net_fd, file_fd;
  struct stat file_stat;
  char client_ip[32], data_arena[65538]; /* Why should we use heap if we can use stack? */
  packet *pkt = (packet *)&data_arena;
  struct sockaddr_in server_addr;
  socklen_t rlen = sizeof(struct sockaddr_in);

  /**
   * Prepare server address data.
   */
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);
  server_addr.sin_addr.s_addr = inet_addr(server_ip);

  /**
   * Create TCP socket.
   */
  if ((net_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Socket creation failed");
    return 1; /* Don't need to close socket fd since it fails to create. */
  }

  /**
   * Conncet to TeaVPN server.
   */
  printf("Connecting to 127.0.0.1:8000...\n");
  if (connect(net_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) < 0) {
    perror("Error on connect");
    goto close_socket;
  }

  printf("Connection established!\n");

ask_file:
  printf("Enter the filename to be sent: ");
  fgets(pkt->filename, 255, stdin);
  pkt->filename_len = strlen(pkt->filename) - 1;
  pkt->filename[pkt->filename_len] = '\0';

  if (stat(pkt->filename, &file_stat) < 0) {
    printf("Cannot stat file \"%s\"\n\n", pkt->filename);
    goto ask_file;
  }

  pkt->file_size = file_stat.st_size;

  printf("File size: %ld bytes\n", pkt->file_size);
  printf("Sending \"%s\"...\n", pkt->filename);

  file_fd = open(pkt->filename, O_RDONLY);
  if (file_fd < 0) {
    printf("Cannot open file!\n");
    goto close_socket;
  }

  read_bytes = read(file_fd, pkt->content,
      (READ_FILE_BUF > pkt->file_size) ? pkt->file_size : READ_FILE_BUF);
  if (read_bytes < 0) {
    perror("Error read()");
    goto close_file;
  }

  #define FIRST_SEND_BYTES \
    sizeof(pkt->filename_len) + \
    sizeof(pkt->filename) + \
    sizeof(pkt->file_size) + \
    read_bytes

  write_bytes = send(net_fd, pkt, FIRST_SEND_BYTES, 0);
  if (write_bytes < 0) {
    perror("Error send()");
    goto close_file;
  }

  sent_file_size += read_bytes;

  while (sent_file_size < (pkt->file_size)) {
    
    read_bytes = read(file_fd, pkt->content, READ_FILE_BUF);
    if (read_bytes < 0) {
      perror("Error read()");
      break;
    }

    write_bytes = send(net_fd, pkt->content, read_bytes, 0);
    if (write_bytes < 0) {
      perror("Error send()");
      break;
    }

    sent_file_size += read_bytes;
  }

  printf("Finished!\n");

close_file:
  close(file_fd);

close_socket:
  close(net_fd);
  return 0;
}

