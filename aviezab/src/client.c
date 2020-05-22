/*
 * First Written : 2020 05 23
 * CLIENT C
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
static struct sockaddr_in server_addr; //, client_addr;
static unsigned int len;
static packet *pkt = (packet *)&data_arena;

int send_file_size(int sockfdd)
{
    pkt->file_size = 0;
    for (;;)
    {
        printf("Client is sending file size information ...\n");
        send(sockfdd, &pkt->file_size, sizeof(pkt), 0);
        printf(">>>> Filesize info: %lu\n", pkt->file_size);
        break;
    }
    return 0;
}

int client_socket_create(int sockfdd)
{
    sockfdd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfdd == -1)
    {
        printf("Socket creation failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully created..\n");
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(IP_ADD);
    server_addr.sin_port = htons(PORT);
    if (connect(sockfdd, (sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        printf("Connection to the server failed...\n");
        return 1;
    }
    else
        printf("Connected to the server..\n");
    send_file_size(sockfdd);
    return 0;
}

int main(int argc, char *argv[], char *envp[])
{
    if (argparser(argc, argv, IP_ADD, &PORT) != 0)
    {
        return 1;
    }
    else
    {
        printf("Client is connected to server on %s %d\n", IP_ADD, PORT);
    }
    if (client_socket_create(sockfd) != 0)
    {
        return 1;
    }
    else
    {
        printf("Now closing socket ..");
        close(sockfd);
    }
}
