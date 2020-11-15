
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "data_structure.h"

static void
handle_client(int client_fd);

static int
server_socket(char *bind_addr, uint16_t bind_port);

int
main(int argc, char *argv[])
{

  if (argc != 3) {
    printf("Usage: %s <bind_addr> <bind_port>\n", argv[0]);
    return 1;
  }

  return server_socket(argv[1], (uint16_t)argv[2]);
}

static int
server_socket(char *bind_addr, uint16_t bind_port)
{
  int net_fd
  int client_fd;
  struct sockaddr_in srv_addr;
  struct sockaddr_in cli_addr;
  
}
