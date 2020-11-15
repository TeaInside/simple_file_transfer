
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

#define HP_CC(CLI_PTR) inet_ntoa((CLI_PTR)->sin_addr), \
                       ntohs((CLI_PTR)->sin_port)

/* Connection task struct. */
typedef struct _conn_task
{
  bool               is_used;  /* Be true if the struct is being used */
  int                cli_fd;   /* Client file descriptor.             */
  size_t             cpos;     /* Count position.                     */
  size_t             clen;     /* Count length.                       */
#if defined(__linux__)
  int                file_fd;  /* To support open O_NONBLOCK fd.      */
#endif
  char               *fbuf;    /* File buffer to reduce write syscall.*/
  FILE               *fhandle; /* To do file operation by using stdio.*/
  packet             *pkt;     /* The packet structure.               */
  struct sockaddr_in cli_addr; /* Client IPv4 address.                */
} conn_task_t;

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
  event_task     *tasks,
  size_t         *free_idx,
  nfds_t         *nfds
);

inline static bool
handle_task(
  size_t      *_i,
  pollfd_t    *clfd,
  event_task  *task,
  size_t      *free_idx,
  nfds_t      *nfds
);

inline static bool
init_task(int cli_fd, event_task *task);

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
static int
event_loop(int net_fd)
{
  int             ret       = 1;    /* The exit code.                 */
  int             timeout   = 3000; /* Poll timeout.                  */
  nfds_t          nfds      = 0;    /* The number of poll fd queue.   */
  pollfd_t        *fds      = NULL; /* fd queue.                      */
  pollfd_t        *clfds    = NULL; /* fd queue, but only for clients.*/
  conn_task_t     *tasks    = NULL; /* State for each client.         */
  size_t          free_idx  = 0;    /* Free index in the tasks.       */
  const size_t    fds_siz   = sizeof(pollfd_t) * (MAX_CONNECTIONS + 1);
  const size_t    tasks_siz = sizeof(conn_task_t) * MAX_CONNECTIONS;


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

  /* Plug socket file descriptor to poll queue. */
  fds[0].fd     = net_fd;
  fds[0].events = POLLIN;
  nfds          = 1;

  /* Poll queue for the clients' file descriptor as array. */
  clfds         = &(fds[1]);

  /* The event loop goes here. */
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
      if (unlikely(stop)) {
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

    if (unlikely(stop)) {
      break;
    }

    for (size_t i = 0; i < free_idx; i++) {
      if (tasks[i].is_used) {
        event_task *task = &(tasks[i]);
        pollfd_t   *clfd = &(clfds[i]);

        if (handle_task(&i, clfd, task, &free_idx, &nfds)) {
          
        }
      }
    }
  }

clean_up:


  /* Notice that it is safe to call free(NULL); */
  free(fds);
  free(tasks);

  return ret;
}


/**
 * @param const pollfd_t *net_pfd
 * @param pollfd_t       *clfds
 * @param event_task     *tasks
 * @param size_t         *free_idx
 * @param nfds_t         *nfds
 * @return bool
 */
inline static bool
accept_cli(
  const pollfd_t *net_pfd,
  pollfd_t       *clfds,
  event_task     *tasks,
  size_t         *free_idx,
  nfds_t         *nfds
)
{
  short revents = net_pfd->revents;

  if (unlikely((revents & POLLIN) != 0)) {

    bool                drop_con; /* Be true if we don't accept conn. */
    int                 cli_fd;   /* Client file descriptor.          */
    struct sockaddr_in  drop_tmp; /* Unaccepted connection address.   */
    event_task          *task      = NULL;
    struct sockaddr_in  *cli_addr  = NULL;
    pollfd_t            *pfd       = NULL;
    socklen_t           rlen       = sizeof(struct sockaddr_in);

    drop_con = (*free_idx >= MAX_CONNECTIONS);

    if (unlikely(drop_con)) {
      /*
       * If the connection entry is full, we accept the new connection 
       * then close it immediately.
       */
      cli_addr = &drop_tmp;
    } else {
      pfd      = &(clfds[*free_idx]);
      task     = &(tasks[*free_idx]);
      cli_addr = &(task->cli_addr);
    }

    cli_fd = accept(net_pfd->fd, (struct sockaddr *)cli_addr, &rlen);

    if (unlikely(cli_fd < 0)) {

      if (errno == EWOULDBLOCK) {
        goto ret_fal;
      }
      printf("Error: accept(): %s\n", strerror(errno));

    } else
    if (unlikely(drop_con)) {

      /* Task array is full. */
      printf("Error: cannot accept more connection!\n");
      goto drop_con;

    } else {

      printf("Accepting connection from %s:%d...\n", HP_CC(cli_addr));
      if (init_task(cli_fd, task)) {
        pfd->fd      = cli_fd;
        pfd->events  = POLLIN;
        pfd->revents = 0;
        *nfds        = *nfds + 1;
        *free_idx    = *free_idx + 1;
      } else {
        printf("Error: cannot create task!\n");
        goto drop_con;
      }
    }


  ret_true:
    return true;

  drop_con:
    printf("Dropping connection from %s:%d\n", HP_CC(cli_addr));
    close(cli_fd);
    goto ret_true;

  } else
  if (unlikely((revents & (POLLHUP | POLLERR | POLLNVAL)) != 0)) {
    stop = true;
  }

ret_fal:
  return false;
}


/**
 * @param size_t      *_i,
 * @param pollfd_t    *clfd,
 * @param event_task  *task,
 * @param size_t      *free_idx,
 * @param nfds_t      *nfds
 * @return bool
 */
inline static bool
handle_task(
  size_t      *_i,
  pollfd_t    *clfd,
  event_task  *task,
  size_t      *free_idx,
  nfds_t      *nfds
)
{

}


/**
 * @param int        cli_fd
 * @param event_task *task
 * @return bool
 */
inline static bool
init_task(int cli_fd, event_task *task)
{
  packet *pkt  = NULL;
  char   *fbuf = NULL;

  pkt = (packet *)malloc(sizeof(packet) + BUFFER_SIZE);
  if (unlikely(pkt == NULL)) {
    printf("Error: cannot allocate memory for packet\n");
    goto err;
  }

  fbuf = (char *)malloc(FILE_BUFFER_SIZ);
  if (unlikely(filebuf == NULL)) {
    printf("Error: cannot allocate memory for filebuf\n");
    goto err;
  }

  task->is_used = true;
  task->cli_fd  = cli_fd;
  task->cpos    = 0;
  task->clen    = 0;
  task->file_fd = -1;
  task->fbuf    = filebuf;
  task->fhandle = NULL;
  task->pkt     = pkt;
  return true;

err:
  free(pkt);
  free(filebuf);
  return false;
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
}
