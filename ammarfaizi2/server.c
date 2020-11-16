
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

#include "vt_hexdump.h"
#include "data_structure.h"

#define likely(EXPR)   __builtin_expect((EXPR), 1)
#define unlikely(EXPR) __builtin_expect((EXPR), 0)

#ifndef OFFSETOFF
#  define OFFSETOFF(TYPE, MEMBER) ((size_t)&(((TYPE *)0)->MEMBER))
#endif

#define MAX_CONNECTIONS (10000ull)
#define FILE_BUFFER_SIZ (4096ull)

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
#else
#  error Compiler is not supported!
#endif



#define HP_CC(CLI_PTR)                                    \
  inet_ntoa(((struct sockaddr_in *)(CLI_PTR))->sin_addr), \
  ntohs(((struct sockaddr_in *)(CLI_PTR))->sin_port)

/* Connection task struct. */
typedef struct _con_task
{
  bool               is_used;  /* Be true if the struct is being used */
  int                cli_fd;   /* Client file descriptor.             */
  size_t             cpos;     /* Count position.                     */
  size_t             clen;     /* Count length.                       */
  char               *fbuf;    /* File buffer to reduce write syscall.*/
  FILE               *fhandle; /* To do file operation by using stdio.*/
  packet             *pkt;     /* The packet structure.               */
  struct sockaddr_in cli_addr; /* Client IPv4 address.                */
  void               *heap;    /* State to save single shared malloc. */
} con_task_t;

typedef struct pollfd pollfd_t;

static int
file_server(char *bind_addr, uint16_t bind_port);

static int
event_loop(int net_fd);

static void
interrupt_handler(int sig);

inline static bool
accept_cli(
  const pollfd_t *net_pfd,
  pollfd_t       *clfds,
  con_task_t     *tasks,
  size_t         *free_idx,
  nfds_t         *nfds
);

inline static bool
handle_task(pollfd_t *clfd, con_task_t *task, bool *close_con);

inline static bool
init_task(int cli_fd, con_task_t *task);

inline static bool
open_fhandle(con_task_t *task);

inline static int
setup_socket(int net_fd);

static const short pfbits = (POLLIN | POLLHUP | POLLERR | POLLNVAL);

bool stop = false;


/** 
 * @param int  argc
 * @param char *argv[]
 * @return int
 */
int
main(int argc, char *argv[])
{

  if (argc != 3) {
    printf("Usage: %s <bind_addr> <bind_port>\n", argv[0]);
    return 1;
  }

  return file_server(argv[1], (uint16_t)atoi(argv[2]));
}


/**
 * @param char      *bind_addr
 * @param uint16_t  bind_port
 * @return int
 */
static int
file_server(char *bind_addr, uint16_t bind_port)
{
  int                retval;
  int                net_fd;
  struct sockaddr_in srv_addr;
  const size_t       srv_addr_siz = sizeof(struct sockaddr_in);

  net_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (net_fd < 0) {
    printf("Error create socket: %s\n", strerror(errno));
    /* No need to close net_fd as it fails to create. */
    return 1;
  }


  if (setup_socket(net_fd) < 0) {
    retval = 1;
    goto close_net_fd;
  }


  /*
   * Prepare server bind address data.
   */
  memset(&srv_addr, 0, sizeof(struct sockaddr_in));
  srv_addr.sin_family      = AF_INET;
  srv_addr.sin_port        = htons(bind_port);
  srv_addr.sin_addr.s_addr = inet_addr(bind_addr);


  retval = bind(net_fd, (struct sockaddr *)&srv_addr, srv_addr_siz);
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
  signal(SIGHUP, interrupt_handler);
  signal(SIGINT, interrupt_handler);
  signal(SIGTERM, interrupt_handler);


  printf("Listening on %s:%d...\n", bind_addr, bind_port);
  fflush(stdout);


  retval = event_loop(net_fd);

close_net_fd:

  /* Close socket file descriptor. */
  close(net_fd);
  return retval;
}


/**
 * @param int net_fd
 * @return int
 */
inline static int
setup_socket(int net_fd)
{
  int val = 1;

  #define SET_X(FD, LEVEL, OPT, VAL, LENP)              \
    if (setsockopt(FD, LEVEL, OPT, VAL, LENP) < 0) {    \
      printf("Error: setsockopt(%s, %s): \"%s\"\n",     \
             #OPT, #VAL, strerror(errno));              \
      return -1;                                        \
    }

  SET_X(net_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));

  #undef SET_X
  return 0;
}


/**
 * @param int net_fd
 * @return int
 */
static int
event_loop(int net_fd)
{
  int             ret       = 1;    /* The exit code.                 */
  int             timeout   = 3000; /* Poll timeout.                  */
  nfds_t          nfds      = 0;    /* The number of poll fd queue.   */
  pollfd_t        *fds      = NULL; /* fd queue.                      */
  pollfd_t        *clfds    = NULL; /* fd queue, but only for clients.*/
  con_task_t      *tasks    = NULL; /* State for each client.         */
  size_t          free_idx  = 0;    /* Free index in the tasks.       */
  const size_t    fds_siz   = sizeof(pollfd_t) * (MAX_CONNECTIONS + 1);
  const size_t    tasks_siz = sizeof(con_task_t) * MAX_CONNECTIONS;


  fds = (pollfd_t *)malloc(fds_siz);
  if (fds == NULL) {
    printf("Error: cannot allocate memory for fds\n");
    goto clean_up;
  }


  tasks = (con_task_t *)malloc(tasks_siz);
  if (tasks == NULL) {
    printf("Error: cannot allocate memory for tasks\n");
    goto clean_up;
  }


  /* Zero the arrays. */
  memset(fds, 0, fds_siz);
  memset(tasks, 0, tasks_siz);


  /* Plug socket file descriptor to poll queue. */
  fds[0].fd      = net_fd;
  fds[0].events  = POLLIN;
  fds[0].revents = 0;
  nfds           = 1;

  /* Poll queue for the clients' file descriptor as array. */
  clfds          = &(fds[1]);


  /* The event loop goes here. */
  while (true) {
    int  rv;      /* Return value of poll(). */

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
      if (errno == EINTR) {
        ret = 0;
      } else {
        printf("Error poll: %s\n", strerror(errno));
      }
      goto clean_up;
    }

    if (unlikely(rv == 0)) {
      if (unlikely(stop)) {
        goto clean_up;
      }
      /* Poll reached its timeout. */
      continue;
    }

    if (unlikely(accept_cli(fds, clfds, tasks, &free_idx, &nfds))) {
      rv--;
    }


    if (unlikely(stop)) {
      goto clean_up;
    }


    for (size_t i = 0; (rv > 0) && (i < free_idx);) {
      if (tasks[i].is_used) {

        con_task_t *task      = &(tasks[i]);
        pollfd_t   *clfd      = &(clfds[i]);
        bool       close_con  = false;

        if (handle_task(clfd, task, &close_con)) {
          rv--;

          if (unlikely(close_con)) {
            size_t copy_siz = (free_idx - 1) - i;

            if (likely(copy_siz > 0)) {
              memmove(clfd, &(clfd[1]), copy_siz * sizeof(pollfd_t));
              memmove(task, &(task[1]), copy_siz * sizeof(con_task_t));
            }

            nfds--;
            free_idx--;
            memset(&(tasks[free_idx]), 0, sizeof(con_task_t));
            continue;
          }
        }
      }
      i++;
    }
  }

clean_up:
  if (likely(tasks != NULL)) {
    for (size_t i = 0; i < free_idx; i++) {
      if (tasks[i].is_used) {
        void *claddr = (void *)&(tasks[i].cli_addr);

        printf("Closing connection from %s:%d...\n", HP_CC(claddr));

        if (tasks[i].fhandle != NULL) {
          fclose(tasks[i].fhandle);
        }

        close(tasks[i].cli_fd);
        free(tasks[i].heap);
      }
    }
  }

  /* Notice that it is safe to call free(NULL); */
  free(fds);
  free(tasks);

  return ret;
}


/**
 * @param const pollfd_t *net_pfd
 * @param pollfd_t       *clfds
 * @param con_task_t     *tasks
 * @param size_t         *free_idx
 * @param nfds_t         *nfds
 * @return bool
 */
inline static bool
accept_cli(
  const pollfd_t *net_pfd,
  pollfd_t       *clfds,
  con_task_t     *tasks,
  size_t         *free_idx,
  nfds_t         *nfds
)
{
  if (likely((net_pfd->revents & pfbits) == 0)) {
    return false;
  }

  bool                drop_con; /* Be true if we don't accept conn. */
  int                 cli_fd;   /* Client file descriptor.          */
  struct sockaddr_in  drop_tmp; /* Unaccepted connection address.   */
  struct sockaddr_in  *cli_addr  = NULL;
  con_task_t          *task      = NULL;
  pollfd_t            *pfd       = NULL;
  size_t              idx        = *free_idx;
  socklen_t           rlen       = sizeof(struct sockaddr_in);

  drop_con = (idx >= MAX_CONNECTIONS);

  if (unlikely(drop_con)) {
    /*
     * If the connection entry is full, we accept the new connection 
     * then close it immediately.
     */
    cli_addr = &drop_tmp;
  } else {
    pfd      = &(clfds[idx]);
    task     = &(tasks[idx]);
    cli_addr = &(task->cli_addr);
  }

  cli_fd = accept(net_pfd->fd, (struct sockaddr *)cli_addr, &rlen);

  if (unlikely(cli_fd < 0)) {
    if (errno != EWOULDBLOCK) {
      printf("Error: accept(): \"%s\"\n", strerror(errno));
    }
    goto ret;
  }


  if (unlikely(drop_con)) {
    printf("Error: cannot accept more connection!\n");
    goto drop_con;
  }


  printf("Accepting connection from %s:%d...\n", HP_CC(cli_addr));

  if (likely(init_task(cli_fd, task))) {
    pfd->fd      = cli_fd;
    pfd->events  = POLLIN;
    pfd->revents = 0;
    *free_idx    = idx + 1;
    *nfds        = *nfds + 1;
  } else {
    printf("Error: cannot create task!\n");
    goto drop_con;
  }


ret:
  return true;

drop_con:
  printf("Dropping connection from %s:%d...\n", HP_CC(cli_addr));
  close(cli_fd);
  goto ret;
}


/**
 * @param int        cli_fd
 * @param con_task_t *task
 * @return bool
 */
inline static bool
init_task(int cli_fd, con_task_t *task)
{
  void   *heap;
  packet *pkt;
  char   *fbuf;
  const size_t pkt_siz  = sizeof(packet) + BUFFER_SIZE;
  const size_t fbuf_siz = FILE_BUFFER_SIZ;

  heap = malloc(pkt_siz + fbuf_siz);
  if (unlikely(heap == NULL)) {
    printf("Error: cannot allocate memory for a new task\n");
    return false;
  }

  pkt           = (packet *)heap;
  fbuf          = &(((char *)heap)[pkt_siz]);

  task->is_used = true;
  task->cli_fd  = cli_fd;
  task->cpos    = 0;
  task->clen    = 0;
  task->fbuf    = fbuf;
  task->fhandle = NULL;
  task->pkt     = pkt;
  task->heap    = heap;
  return true;
}


/**
 * @param pollfd_t    *clfd
 * @param con_task_t  *task
 * @param bool        *close_con
 * @return bool
 */
inline static bool
handle_task(pollfd_t *clfd, con_task_t *task, bool *close_con)
{
  if ((clfd->revents & pfbits) == 0) {
    return false;
  }

  char        *buf;
  packet      *pkt;
  ssize_t     recv_l;
  void        *claddr = (void *)&(task->cli_addr);

  clfd->revents = 0;
  pkt           = task->pkt;
  buf           = &(((char *)pkt)[task->cpos]);
  recv_l        = recv(clfd->fd, buf, BUFFER_SIZE, 0);

  if (unlikely(recv_l < 0)) {

    if (errno != EWOULDBLOCK) {
      char *errstr = strerror(errno);
      printf("Error: recv() [%s:%d]: \"%s\"\n", HP_CC(claddr), errstr);
      goto close_cli;
    }

    goto ret;
  }


  if (unlikely(recv_l == 0)) {
    /* Client has been disconnected. */
    goto close_cli;
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

        if (unlikely(!open_fhandle(task))) {
          goto close_cli;
        }

        printf("Receiving file from %s:%d...\n", HP_CC(claddr));

        task->cpos     = min_size;
        pkt->file_size = be64toh(pkt->file_size);
        fhandle        = task->fhandle;
        fwrite_siz     = total_siz - min_size;
      } else {
        fhandle        = task->fhandle;
        fwrite_siz     = recv_siz;
      }

      content_siz = total_siz - min_size;

      if (likely(fwrite_siz > 0)) {
        /* Write the file. */
        fwrite(pkt->content, fwrite_siz, sizeof(char), fhandle);
      }

      if (unlikely(pkt->file_size <= content_siz)) {
        printf("File received completely!\n");
        goto close_cli;
      }

    } else {
      /* We haven't received the file info in details. */
      task->cpos = total_siz;
    }
  }

ret:
  return true;

close_cli:
  printf("Closing connection from %s:%d...\n", HP_CC(claddr));

  if (task->fhandle != NULL) {
    /* We must not free the task->heap before this fclose. */
    fclose(task->fhandle);
  }

  close(task->cli_fd);
  free(task->heap);
  *close_con = true;
  goto ret;
}


/**
 * @param con_task_t *task
 * @return bool
 */
inline static bool
open_fhandle(con_task_t *task)
{
  #define bbfn_siz (sizeof("uploaded_files/"))
  #define bfn_siz  (sizeof("uploaded_files/") + 255)

  FILE          *fhandle     = NULL;
  packet        *pkt         = task->pkt;
  uint8_t       fname_len    = pkt->filename_len;
  char          bfn[bfn_siz] = "uploaded_files/";

  /* For safety. */
  pkt->filename[(fname_len == 0xff) ? 254 : fname_len] = '\0';

  memcpy(&(bfn[bbfn_siz - 1]), pkt->filename, fname_len);

  #undef bbfn_siz
  #undef bfn_siz

#if defined(__linux__)
  {
    /*
     * I use syscall open() on Linux to apply O_NONBLOCK flag.
     */
    const int flags = O_CREAT | O_WRONLY | O_NONBLOCK;
    const int mode  = S_IRWXU | S_IRGRP | S_IROTH;
    int       fd    = open(bfn, flags, mode);

    if (unlikely(fd == -1)) {
      printf("Error: open(): \"%s\"\n", strerror(errno));
      return false;
    }

    fhandle = fdopen(fd, "wb");
    if (unlikely(fhandle == NULL)) {
      printf("Error: cannot load file descriptor to fdopen (%d)\n", fd);
      close(fd);
      return false;
    }
  }
#else
  fhandle = fopen(bfn, "wb");
  if (unlikely(fhandle == NULL)) {
    printf("Error: create file: \"%s\"\n", bfn);
    return false;
  }
#endif

  /*
   * Set full buffered to reduce syscall write().
   */
  setvbuf(fhandle, task->fbuf, _IOFBF, FILE_BUFFER_SIZ);

  task->fhandle = fhandle;
  return true;
}


/**
 * @param int sig
 * @return void
 */
static void
interrupt_handler(int sig)
{
  (void)sig;
  stop = true;
  putchar('\n');
}
