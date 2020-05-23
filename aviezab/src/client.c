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
char data_arena[65538];
uint16_t PORT = 9090;
char IP_ADD[16] = "0.0.0.0";
static int sockfd, clientfd;
static struct sockaddr_in server_addr;
static unsigned int len;
static packet *pkt = (packet *)&data_arena;

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

#define FIRST_SEND_BYTES         \
    sizeof(pkt->filename_len) +  \
        sizeof(pkt->filename) +  \
        sizeof(pkt->file_size) + \
        file_read_bytes

#define FIRST_READ_BYTES 4096

    {
        int ret;
        struct stat st;
        ssize_t send_ret, read_ret;
        uint64_t file_read_bytes = 0;

        int file_fd = open(pkt->filename, O_RDONLY);
        if (file_fd < 0)
        {
            perror("!!>>>  Error open(1)");
            printf("!!>>> Cannot open file: %s\n", pkt->filename);
            return -1;
        }

/* Debug only. */
#if 1
        printf("Filename = %s\n", pkt->filename);
        printf("Filename_len = %d\n", pkt->filename_len);
        printf("File_size = %ld\n", pkt->file_size);
#endif

        /* Read file. */
        {
            /* Read head of file. */
            read_ret = read(file_fd, pkt->content, FIRST_READ_BYTES);
            if (read_ret < 0)
            {
                perror("!!>>> Error read(1)");
                printf("Error while reading file: %s\n", pkt->filename);
                ret = -1;
                goto close_file;
            }

            /* We have read read_ret bytes. */
            file_read_bytes += (uint64_t)read_ret;
        }

        /* Send file information. */
        {
            send_ret = send(sockfdd, pkt, FIRST_SEND_BYTES, 0);
            if (send_ret < 0)
            {
                perror("!!>>> Error send(1)");
                printf("!!>>> Error while sending file information\n");
                ret = -1;
                goto close_file;
            }
        }

        /* Send the rest of file content. */
        {
            while (file_read_bytes < pkt->file_size)
            {

                /* Read file content. */
                read_ret = read(file_fd, pkt->content, FIRST_READ_BYTES);
                if (read_ret < 0)
                {
                    perror("!!>>> Error read()");
                    printf("!!>>> Error while reading file: %s\n", pkt->filename);
                    ret = -1;
                    goto close_file;
                }

                /* Send it to the server. */
                send_ret = send(sockfdd, pkt->content, read_ret, 0);
                if (send_ret < 0)
                {
                    perror("!!>>> Error send(2)");
                    printf("!!>>> Error while sending file content\n");
                    ret = -1;
                    goto close_file;
                }

                file_read_bytes += (uint64_t)read_ret;
            }
        }

        if (send_ret < 0)
        {
            perror("!!>>> Error send(3)");
            return -1;
        }

    close_file:
        close(file_fd);
    ret:
        return ret;
    }
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
        printf(">>>>> Client will be connected to server on %s %d\n", IP_ADD, PORT);
    cobacoba:
        printf(">>>>> Input filename: ");
        memset(pkt->filename, 0, sizeof(pkt->filename));
        fgets(pkt->filename, 255, stdin);
        pkt->filename_len = strlen(pkt->filename) - 1;
        pkt->filename[pkt->filename_len] = '\0';
        ssize_t fs = findfileSize(pkt->filename);
        if (fs >= 0)
        {
            pkt->file_size = (uint64_t)fs;
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