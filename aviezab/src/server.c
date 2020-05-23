/*
 * First Written : 2020 05 13
 * SERVER C
 * Written by    : Aviezab
 * MIT License
 * Clang-format
 */
#include <avzb.h>
#include <data_structure.h>
#include <instant.h>

#define sockaddr struct sockaddr

uint16_t PORT = 9090;
char IP_ADD[16] = "0.0.0.0";
static int sockfd, clientfd;
static struct sockaddr_in server_addr, client_addr;
static unsigned int len;
static packet *pkt = (packet *)&data_arena;
#define FILE_INFO_SIZE            \
  (size_t)(                       \
      sizeof(pkt->filename_len) + \
      sizeof(pkt->filename) +     \
      sizeof(pkt->file_size))

int receive_file_content(int sockfdd)
{
  int file_fd;
  char target_file[512];
  ssize_t read_bytes = 0, write_bytes = 0;
  size_t file_read_bytes = 0, read_ok_bytes = 0;
  snprintf(target_file, 512, "uploaded_files/%s", pkt->filename);
  printf("=== File Info ===\n");
  printf("Filename: \"%s\"\n", pkt->filename);
  printf("File size: %ld\n", pkt->file_size);
  printf("=====================\n");
  printf("The file will be stored at: \"%s\"\n", target_file);
  printf("Waiting for file...\n");

  /**
   * Receive file info.
   * Make sure file info is completely received before processing file.
   */
  do
  {

    read_bytes = recv(sockfdd, ((char *)pkt) + read_ok_bytes, FILE_INFO_SIZE + BUFFER_SIZE, 0);
    if (read_bytes < 0)
    {
      perror("Error recv(1)");
      close(file_fd);
      return 1;
    }

    read_ok_bytes += (size_t)read_bytes;

  } while (read_ok_bytes < FILE_INFO_SIZE);

  file_read_bytes = read_ok_bytes - FILE_INFO_SIZE;

  printf("=== File Info ===\n");
  printf("Filename: \"%s\"\n", pkt->filename);
  printf("File size: %ld\n", pkt->file_size);
  printf("=====================\n");
  printf("The file will be stored at: \"%s\"\n", target_file);

  /**
   * Create and open file.
   */
  file_fd = open(target_file, O_CREAT | O_WRONLY, S_IRWXU | S_IRGRP | S_IROTH);
  if (file_fd < 0)
  {
    perror("Error open (1)");
    printf("Cannot create file: \"%s\"\n", target_file);
    close(file_fd);
    return 2;
  }
  /**
   * Write some received data.
   */
  write_bytes = write(file_fd, pkt->content, file_read_bytes);
  if (write_bytes < 0)
  {
    perror("Error write (1)");
    close(file_fd);
    return 3;
  }

  printf("Receiving file...\n");

  /**
   * Receiving file...
   */
  while (file_read_bytes < pkt->file_size)
  {

    /**
     * Reading from socket...
     */
    read_bytes = recv(sockfdd, pkt->content, BUFFER_SIZE, 0);
    if (read_bytes < 0)
    {
      perror("Error recv(2)");
      close(file_fd);
      return 1;
    }

    file_read_bytes += read_bytes;

    /**
     * Writing to file...
     */
    write_bytes = write(file_fd, pkt->content, read_bytes);
    if (write_bytes < 0)
    {
      perror("Error write (2)");
      close(file_fd);
      return 3;
    }
  }

  printf("File received completely!\n\n");
  return 0;
}

int receive_filename_char(int sockfdd)
{

  printf(">>>>> Server is receiving filename information ...\n");

  if (recv(sockfdd, &pkt->filename, pkt->filename_len, 0))
  {
    printf(">>>>> Filename info: %s\n", pkt->filename);

    return 0;
  }
  else
  {
    return 1;
  }
}

int receive_filename_len(int sockfdd)
{
  uint8_t ulang = 1;
ulang_rcv_fl:
  pkt->filename_len = 0;
  for (;;)
  {
    printf(">>>>> Server is receiving filename length information ...\n");
    recv(sockfdd, &pkt->filename_len, sizeof(pkt), 0);
    printf(">>>>> Filename length info: %hhu\n", pkt->filename_len);
    break;
  }
  if (pkt->filename_len == 0)
  {
    if (ulang != 3)
    {
      printf("!>>>> Filename length is zero. Repeating the process until 3 times..\n");
      ulang++;
      goto ulang_rcv_fl;
    }
    else
    {
      return 1;
    }
  }
  return 0;
}

int receive_file_size(int sockfdd)
{
  uint8_t ulang = 1;
ulang_rcv_fs:
  pkt->file_size = 0;
  for (;;)
  {
    printf(">>>>> Server is receiving file size information ...\n");
    recv(sockfdd, &pkt->file_size, sizeof(pkt), 0);
    printf(">>>>> Filesize info: %lu\n", pkt->file_size);
    break;
  }
  if (pkt->file_size == 0)
  {
    if (ulang != 3)
    {
      printf("!>>> Filesize info is zero. Repeating the process until 3 times..\n");
      ulang++;
      goto ulang_rcv_fs;
    }
    else
    {
      return 1;
    }
  }
  return 0;
}

int server_socket_create(int sockfdd)
{
  sockfdd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfdd == -1)
  {
    printf("!!>>> Socket creation failed...\n");
    exit(0);
  }
  else
    printf(">>>>> Socket successfully created..\n");

  memset(&server_addr, 0, sizeof(server_addr));
  memset(&client_addr, 0, sizeof(client_addr));
  // assign IP, PORT
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(IP_ADD);
  server_addr.sin_port = htons(PORT);
  if ((bind(sockfdd, (sockaddr *)&server_addr, sizeof(server_addr))) != 0)
  {
    printf("!!>>> Socket bind failed...\n");
    return 1;
  }
  else
  {
    printf(">>>>> Socket successfully binded..\n");
  }

  if ((listen(sockfdd, 5)) != 0)
  {
    printf("!!>>> Listen failed...\n");
    return 1;
  }
  else
    printf(">>>>> Server listening..\n");
  len = sizeof(client_addr);

  // Accept the data packet from client and verification
  clientfd = accept(sockfdd, (sockaddr *)&client_addr, &len);
  printf(">>>>> Accepting client (%s:%d)...\n", inet_ntoa(client_addr.sin_addr),
         ntohs(client_addr.sin_port));
  if (clientfd < 0)
  {
    printf("!!>>> Server acccept failed...\n");
    return 1;
  }
  else
    printf(">>>>> Server acccepts the Client...\n");
  int status_server = 0;
  for (;;)
  {
    status_server = receive_file_size(clientfd);
    if (status_server == 0)
    {
      break;
    }
    else
    {
      printf("!!>>> Error at receive_file_size\n");
      break;
    }
  }
  for (;;)
  {
    status_server = receive_filename_len(clientfd);
    if (status_server == 0)
    {
      break;
    }
    else
    {
      printf("!!>>> Error at receive_filename_len\n");
      break;
    }
  }
  for (;;)
  {
    status_server = receive_filename_char(clientfd);
    if (status_server == 0)
    {
      break;
    }
    else
    {
      printf("!!>>> Error at receive_filename_char\n");
      break;
    }
  }

  for (;;)
  {
    status_server = receive_file_content(clientfd);
    if (status_server == 0)
    {
      break;
    }
    else
    {
      printf("!!>>> Error at receive_file_content\n");
      break;
    }
  }

  return status_server;
}

int main(int argc, char *argv[], char *envp[])
{

  if (argparser(argc, argv, IP_ADD, &PORT) != 0)
  {
    return 1;
  }
  else
  {
    printf("Server is running on %s %d\n", IP_ADD, PORT);
  }
  if (server_socket_create(sockfd) != 0)
  {
    return 1;
  }
  else
  {
    printf("Now closing socket ..");
    close(sockfd);
  }
}
