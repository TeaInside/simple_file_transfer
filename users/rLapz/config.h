#define DEST_DIR          "uploaded_files"
#define FILE_NAME_LEN     255u
#define BUFFER_SIZE       6969u

#define BACKLOG           10   /* listen() backlog size */
#define INIT_CLIENT_SIZE  5
#define MAX_CLIENTS       100u


/* colors */
#define BOLD_GREEN(TEXT)  "\x1b[01;32m" TEXT "\x1b[00m"
#define BOLD_RED(TEXT)    "\x1b[01;31m" TEXT "\x1b[00m"
#define BOLD_WHITE(TEXT)  "\x1b[01;37m" TEXT "\x1b[00m"
#define BOLD_YELLOW(TEXT) "\x1b[01;33m" TEXT "\x1b[00m"
