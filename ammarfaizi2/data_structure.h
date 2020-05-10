
#ifndef DATA_STRUCTURE_H
#define DATA_STRUCTURE_H

#include <stdint.h>
#define BUFFER_SIZE 3000
#define READ_FILE_BUF 2000
typedef struct __attribute__((__packed__)) {
  uint8_t filename_len; // the length of filename.
  char filename[255];   // filename.
  uint64_t file_size;   // the size of file to be sent.
  char content[1];      // the file content, must be "struct hack".
} packet;

#endif
