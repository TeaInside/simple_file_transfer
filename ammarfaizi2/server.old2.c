
/**
 * @author Ammar Faizi <ammarfaizi2@gmail.com>
 * @license MIT
 */

#include <poll.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "data_structure.h"

#define likely(EXPR)   __builtin_expect((EXPR), 1)
#define unlikely(EXPR) __builtin_expect((EXPR), 0)

#ifndef OFFSETOFF
#  define OFFSETOFF(TYPE, MEMBER) ((size_t)&(((TYPE *)0)->MEMBER))
#endif

#define MAX_CONNECTIONS (5ull)
#define FILE_BUFFER_SIZ (8192ull)

#ifndef BUFFER_SIZE
#  define BUFFER_SIZE     (4096)
#endif

#if defined(__linux__)
#  include <endian.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/endian.h>
#elif defined(__OpenBSD__)
#  include <sys/types.h>
#  define be16toh(x) betoh16(x)
#  define be32toh(x) betoh32(x)
#  define be64toh(x) betoh64(x)
#endif

static int
server_socket(char *bind_addr, uint16_t bind_port);

static int
event_loop(int net_fd);

static void
interrupt_handler(int sig);

bool interrupted = false;

int
main(int argc, char *argv[])
{

  if (argc != 3) {
    printf("Usage: %s <bind_addr> <bind_port>\n", argv[0]);
    return 1;
  }

  return server_socket(argv[1], (uint16_t)atoi(argv[2]));
}

static int
server_socket(char *bind_addr, uint16_t bind_port)
{
  int                retval;
  int                net_fd;
  struct sockaddr_in srv_addr;

  retval = 1;

  /*
   * Prepare server bind address data.
   */
  memset(&srv_addr, 0, sizeof(struct sockaddr_in));
  srv_addr.sin_family      = AF_INET;
  srv_addr.sin_port        = htons(bind_port);
  srv_addr.sin_addr.s_addr = inet_addr(bind_addr);

  net_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (net_fd < 0) {
    printf("Error create socket: %s\n", strerror(errno));
    /* No need to close net_fd as it fails to create. */
    return 1;
  }

  retval = bind(net_fd, (struct sockaddr *)&srv_addr,
                sizeof(struct sockaddr_in));

  if (retval < 0) {
    printf("Error bind socket: %s\n", strerror(errno));
    retval = 1;
    goto close_net_fd;
  }

  retval = listen(net_fd, 10);

  if (retval < 0) {
    printf("Error listen socket: %s\n", strerror(errno));
    retval = 1;
    goto close_net_fd; 
  }

  /* Ignore SIGCHLD and SIGPIPE */
  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  /* Add interrupt and terminate handler. */
  signal(SIGINT, interrupt_handler);
  signal(SIGTERM, interrupt_handler);

  printf("Listening on %s:%d...\n", bind_addr, bind_port);
  fflush(stdout);

  retval = event_loop(net_fd);

close_net_fd:
  close(net_fd);
  return retval;
}

typedef struct _event_task
{
  bool                is_used;
  int                 cli_fd;
  size_t              cpos;
  size_t              clen;
  struct sockaddr_in  cli_addr;
#if defined(__linux__)
  int                 file_fd;
#endif
  FILE                *fhandle;
  packet              *pkt;
  char                *filebuf;
} event_task;

typedef struct pollfd pollfd_t;

inline static bool
accept_cli(
  const pollfd_t *net_pfd,
  pollfd_t       *clfds,
  event_task     *tasks,
  size_t         *free_idx,
  nfds_t         *nfds
)
{

  if (unlikely((net_pfd->revents & (POLLIN | POLLHUP | POLLERR |
                                    POLLNVAL)) != 0)) {
    bool                drop_con;
    int                 cli_fd;
    struct sockaddr_in  drop_tmp;
    event_task          *task      = NULL;
    struct sockaddr_in  *cli_addr  = NULL;
    socklen_t           rlen       = sizeof(struct sockaddr_in);

    drop_con = (*free_idx >= MAX_CONNECTIONS);

    if (unlikely(drop_con)) {
      /*
       * If the connection entry is full, we accept the new connection 
       * then close it.
       */
      cli_addr = &drop_tmp;
    } else {
      task     = &(tasks[*free_idx]);
      cli_addr = &(task->cli_addr);
    }

    cli_fd = accept(net_pfd->fd, (struct sockaddr *)cli_addr, &rlen);

    if (unlikely(cli_fd < 0)) {
      printf("Error accept: %s\n", strerror(errno));
    } else
    if (unlikely(drop_con)) {
      printf("Cannot accept more connection!\n");
      printf("Dropping connection from (%s:%d)\n",
             inet_ntoa(cli_addr->sin_addr), ntohs(cli_addr->sin_port));

      close(cli_fd);
    } else {

      printf("Accepting connection from (%s:%d)...\n",
             inet_ntoa(cli_addr->sin_addr), ntohs(cli_addr->sin_port));

      task->is_used = true;
      task->cli_fd  = cli_fd;
      task->cpos    = 0;
      task->clen    = 0;
      task->fhandle = NULL;
      task->pkt     = (packet *)malloc(sizeof(packet) + BUFFER_SIZE);
      task->filebuf = (char *)malloc(FILE_BUFFER_SIZ);

      memset(task->pkt, 0, sizeof(packet) + BUFFER_SIZE);

      clfds[*free_idx].fd     = cli_fd;
      clfds[*free_idx].events = POLLIN;
      *nfds                   = *nfds + 1;
      *free_idx               = *free_idx + 1;
    }

    return true;
  }

  return false;
}


inline static bool
handle_client(
  size_t      *_i,
  pollfd_t    *clfd,
  event_task  *task,
  size_t      *free_idx,
  nfds_t      *nfds
)
{
  volatile size_t i = *_i;


  if ((clfd->revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
    char    *buf;
    ssize_t recv_l;
    packet  *pkt;

    pkt    = task->pkt;
    buf    = &(((char *)pkt)[task->cpos]);
    recv_l = recv(clfd->fd, buf, BUFFER_SIZE, 0);

    if (unlikely(recv_l < 0)) {

      if (errno == EWOULDBLOCK) {
        goto ret_true;
      }

      printf("Error recv: %s\n", strerror(errno));
      goto close_client;
    }

    if (unlikely(recv_l == 0)) {
      /* Client has been disconnected. */
      goto close_client;
    }


    {
      size_t recv_siz     = (size_t)recv_l;
      size_t min_size     = OFFSETOFF(packet, content);
      size_t total_siz    = task->clen + recv_siz;
      task->clen          = total_siz;

      if (likely(total_siz >= min_size)) {

        /*
         * If total_siz is greater than or equals to min_size, meaning
         * that we have received the file information, such as
         * filename_len, filename, file_size and maybe partial bytes
         * of then content. Therefore we are receiving the content of
         * the file.
         */

        FILE   *fhandle;
        size_t fwrite_siz;
        size_t content_siz;

        if (unlikely(task->fhandle == NULL)) {
          char bfn[sizeof("uploaded_files/") + 255 + 1];

          /* For safety. */
          pkt->filename[sizeof(pkt->filename) - 1] = '\0';
          sprintf(bfn, "uploaded_files/%s", pkt->filename);

#if defined(__linux__)
          {
            /*
             * I use syscall open() on Linux to apply O_NONBLOCK flag.
             */
            const int flags = O_CREAT | O_WRONLY | O_NONBLOCK;
            const int mode  = S_IRWXU | S_IRGRP | S_IROTH;
            task->file_fd   = open(bfn, flags, mode);

            if (unlikely(task->file_fd == -1)) {
              printf("Cannot create file: \"%s\"\n", bfn);
              goto close_client;
            }

            fhandle         = fdopen(task->file_fd, "wb");
          }
#else
          fhandle = fopen(bfn, "wb");
#endif
          if (unlikely(fhandle == NULL)) {
            printf("Cannot create file: \"%s\"\n", bfn);
            goto close_client;
          }

          /*
           * Set full buffered to reduce syscall write().
           */
          setvbuf(fhandle, task->filebuf, _IOFBF, FILE_BUFFER_SIZ);

          printf("Receiving file...\n");

          task->cpos     = min_size;
          task->fhandle  = fhandle;
          fwrite_siz     = total_siz - min_size;
          pkt->file_size = be64toh(pkt->file_size);
        } else {
          fhandle        = task->fhandle;
          fwrite_siz     = recv_siz;
        }

        content_siz = total_siz - min_size;

        /* Write the file. */
        fwrite(pkt->content, fwrite_siz, sizeof(char), fhandle);


        if (unlikely(pkt->file_size <= content_siz)) {
          printf("File received completely!\n");
          goto close_client;
        }

      } else {

        /* We haven't received the file info in details. */
        task->cpos = total_siz;
      }

    }

  ret_true:
    return true;

  close_client:
    {
      size_t             copy_siz;
      struct sockaddr_in *cli_addr = &(task->cli_addr);

      printf("Connection closed (%s:%d)\n",
             inet_ntoa(cli_addr->sin_addr), ntohs(cli_addr->sin_port));

      if (task->fhandle != NULL) {
        /* You must not free the task->filebuf before this point. */
        fclose(task->fhandle);
      }

      free(task->pkt);
      free(task->filebuf);
      close(clfd->fd);


      if (*free_idx <= (MAX_CONNECTIONS - 1)) {
        copy_siz = (*free_idx - 1) - i;
      } else {
        copy_siz = (MAX_CONNECTIONS - 1) - i;
      }

      if (likely(copy_siz > 0)) {
        memcpy(clfd, &(clfd[1]), copy_siz * sizeof(pollfd_t));
        memcpy(task, &(task[1]), copy_siz * sizeof(event_task));
        task      = &(task[*free_idx - i]);
      }

      *nfds     = *nfds - 1;
      *free_idx = *free_idx - 1;
      memset(task, 0, sizeof(event_task));

      *_i = *_i - 1;
    }

    goto ret_true;
  }

  return false;
}


static int
event_loop(int net_fd)
{
  int             ret       = 1;
  int             timeout   = 3000;
  nfds_t          nfds      = 0;
  pollfd_t        *fds      = NULL;
  pollfd_t        *clfds    = NULL;
  event_task      *tasks    = NULL;
  size_t          free_idx  = 0;
  const size_t    fds_siz   = sizeof(pollfd_t) * (MAX_CONNECTIONS + 1);
  const size_t    tasks_siz = sizeof(event_task) * MAX_CONNECTIONS;

  fds = (pollfd_t *)malloc(fds_siz);
  if (fds == NULL) {
    printf("Error: cannot allocate memory for fds\n");
    goto clean_up;
  }

  tasks = (event_task *)malloc(tasks_siz);
  if (tasks == NULL) {
    printf("Error: cannot allocate memory for tasks\n");
    goto clean_up;
  }

  /* Zero the arrays. */
  memset(fds, 0, fds_siz);
  memset(tasks, 0, tasks_siz);

  fds[0].fd     = net_fd;
  fds[0].events = POLLIN;
  nfds          = 1;
  clfds         = &(fds[1]);

  /* The event loop. */
  while (true) {
    int  rv;      /* Return value of poll()       */
    bool acc_ret; /* Return value of accept_cli() */

    /*
     * Notice that the poll will return the number of ready file
     * descriptors.
     *
     * Exceptions:
     * 1. If it returns 0, meaning that the poll reached its timeout.
     * 2. If it returns negative value, meaning that an error occured.
     *
     */

    rv = poll(fds, nfds, timeout);

    if (unlikely(rv < 0)) {
      printf("Error poll: %s\n", strerror(errno));
      break;
    }

    if (unlikely(rv == 0)) {

      if (unlikely(interrupted)) {
        break;
      }

      /* Poll reached its timeout. */
      continue;
    }

    acc_ret = accept_cli(fds, clfds, tasks, &free_idx, &nfds);

    if (unlikely(acc_ret)) {
      /* If rv is zero, there are no other ready file descriptors. */
      if (--rv) continue;
    }

    for (size_t i = 0; (i < MAX_CONNECTIONS) && (rv > 0); i++) {
      if (tasks[i].is_used) {
        event_task *task = &(tasks[i]);
        pollfd_t   *clfd = &(clfds[i]);

        if (handle_client(&i, clfd, task, &free_idx, &nfds)) {
          rv--;
        }
      }
    }
  }

clean_up:
  if (tasks != NULL) {
    for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
      if (tasks[i].is_used) {
        if (tasks[i].fhandle != NULL) {
          fclose(tasks[i].fhandle);
        }
        free(tasks[i].pkt);
        close(tasks[i].cli_fd);
      }
    }
  }

  free(fds);
  free(tasks);
  return ret;
}


static void
interrupt_handler(int sig)
{
  interrupted = true;
  (void)sig;
}
