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

int send_file_content(int sockfdd, char filepath[])
{
    FILE *file = NULL;
    //unsigned char buffer[1024]; // array of bytes, not pointers-to-bytes
    size_t bytesRead = 0;
    file = fopen(filepath, "rb");
    if (file != NULL)
    {
        // read up to sizeof(buffer) bytes
        // selama bytes dibaca lebih dari 0, tandanya belum selesai kirim.
        while ((bytesRead = fread(pkt->content, 1, sizeof(pkt->content), file)) > 0)
        {
            send(sockfdd, pkt->content, sizeof(pkt->content), 0);
            printf("%s", pkt->content);
        }
    }
    return 0;
}

int send_filename_char(int sockfdd, char filepath[])
{

    printf(">>>>> Client is sending filename information ...\n");
    if (send(sockfdd, &pkt->filename, sizeof(pkt->filename), 0))
    {
        printf(">>>>> Filename info: %s\n", pkt->filename);

        return 0;
    }
    else
    {
        return 1;
    }
}

int send_filename_len(int sockfdd, char filepath[])
{
    ssize_t fs = strlen(filepath);
    if (fs >= 0)
    {
        pkt->filename_len = strlen(filepath);
    }
    else
    {
        return -1;
    }
    for (;;)
    {
        printf(">>>>> Client is sending filename length information ...\n");
        send(sockfdd, &pkt->filename_len, sizeof(pkt->filename_len), 0);
        printf(">>>>> Filename info: %hhu\n", pkt->filename_len);
        break;
    }
    return 0;
}

int send_file_size(int sockfdd, char filepath[])
{
    ssize_t fs = findfileSize(filepath);
    if (fs >= 0)
    {
        pkt->file_size = findfileSize(filepath);
    }
    else
    {
        return -1;
    }
    for (;;)
    {
        printf(">>>>> Client is sending file size information ...\n");
        send(sockfdd, &pkt->file_size, sizeof(pkt->file_size), 0);
        printf(">>>>> Filesize info: %lu\n", pkt->file_size);
        break;
    }
    return 0;
}

int client_socket_create(int sockfdd)
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
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(IP_ADD);
    server_addr.sin_port = htons(PORT);
    if (connect(sockfdd, (sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        printf("!!>>> Connection to the server failed...\n");
        return 1;
    }
    else
        printf(">>>>> Connected to the server..\n");
    int status_client = 0;
    for (;;)
    {
        status_client = send_file_size(sockfdd, pkt->filename);
        if (status_client == 0)
        {
            break;
        }
        else
        {
            printf("!!>>> Error at send_file_size\n");
            break;
        }
    }
    for (;;)
    {
        status_client = send_filename_len(sockfdd, pkt->filename);
        if (status_client == 0)
        {
            break;
        }
        else
        {
            printf("!!>>> Error at send_filename_len\n");
            break;
        }
    }
    for (;;)
    {
        status_client = send_filename_char(sockfdd, pkt->filename);
        if (status_client == 0)
        {
            break;
        }
        else
        {
            printf("!!>>> Error at send_filename_char\n");
            break;
        }
    }

    for (;;)
    {
        status_client = send_file_content(sockfdd, pkt->filename);
        if (status_client == 0)
        {
            break;
        }
        else
        {
            printf("!!>>> Error at send_file_content\n");
            break;
        }
    }
    return status_client;
}

int main(int argc, char *argv[], char *envp[])
{
    int cobafile = 1;
    if (argparser(argc, argv, IP_ADD, &PORT) != 0)
    {
        return 1;
    }
    else
    {
        printf(">>>>> Client is connected to server on %s %d\n", IP_ADD, PORT);
    cobacoba:
        printf(">>>>> Input filename: ");
        memset(pkt->filename, 0, sizeof(pkt->filename));
        fgets(pkt->filename, 255, stdin);
        pkt->filename_len = strlen(pkt->filename) - 1;
        pkt->filename[pkt->filename_len] = '\0';
        printf("%s", pkt->filename);
        ssize_t fs = findfileSize(pkt->filename);
        if (fs >= 0)
        {
        }
        else
        {

            if (cobafile != 3)
            {
                printf("!>>>> File doesn't exist. Input again!\n");
                cobafile++;
                goto cobacoba;
            }
            printf(">>>>> Stonehead. Now closing socket ..");
            close(sockfd);
            return 0;
        }
        if (client_socket_create(sockfd) != 0)
        {
            return 1;
        }
        else
        {
            printf(">>>>> Now closing socket ..");
            close(sockfd);
        }
    }
}
