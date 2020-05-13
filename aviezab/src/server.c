/*
 * First Written : 2020 05 13
 * CLIENT C
 * Written by    : Aviezab
 * MIT License
 * Clang-format
 */
#include <avzb.h>
#include <data_structure.h>
#include <instant.h>

#define sockaddr struct sockaddr

int argparser(int argcc, char *argvv[], char *IP_ADDD);
uint16_t set_port(uint16_t PORTX);
static bool str_to_uint16(const char *str, uint16_t *res);

uint16_t PORT = 9090;
char IP_ADD[16] = "0.0.0.0";
static int sockfd, clientfd;
static struct sockaddr_in serveraddr, client_addr;
static unsigned int len;
static packet *pkt = (packet *)&data_arena;

int server_socket_create(int sockfdd) {
  sockfdd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfdd == -1) {
    printf("Socket creation failed...\n");
    exit(0);
  } else
    printf("Socket successfully created..\n");

  memset(&serveraddr, 0, sizeof(serveraddr));
  memset(&client_addr, 0, sizeof(client_addr));
  // assign IP, PORT
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = inet_addr(IP_ADD);
  serveraddr.sin_port = htons(PORT);
  // printf("%d %s", sockfdd, serveraddr);
  if ((bind(sockfdd, (sockaddr *)&serveraddr, sizeof(serveraddr))) != 0) {
    printf("Socket bind failed...\n");
    return 1;
  } else {
    printf("Socket successfully binded..\n");
  }

  if ((listen(sockfdd, 5)) != 0) {
    printf("Listen failed...\n");
    return 1;
  } else
    printf("Server listening..\n");
  len = sizeof(client_addr);

  // Accept the data packet from client and verification
  clientfd = accept(sockfdd, (sockaddr *)&client_addr, &len);
  printf("Accepting client (%s:%d)...\n", inet_ntoa(client_addr.sin_addr),
         ntohs(client_addr.sin_port));
  if (clientfd < 0) {
    printf("Server acccept failed...\n");
    return 1;
  } else
    printf("Server acccepts the Client...\n");
  printf("Now closing..\n");
  close(clientfd);
  return 0;
}

int main(int argc, char *argv[], char *envp[]) {

  if (argparser(argc, argv, IP_ADD) != 0) {
    return 1;
  } else {
    // str_to_uint16(argv[2], &PORT);
    printf("Server is running on %s %d\n", IP_ADD, PORT);
  }
  if (server_socket_create(sockfd) != 0) {
    return 1;
  } else {
    printf("Now closing socket ..");
    close(sockfd);
  }
}
