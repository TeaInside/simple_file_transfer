
#ifndef DATA_STRUCTURE_H
#define DATA_STRUCTURE_H

#include <stdint.h>
#define BUFFER_SIZE 1500

typedef struct __attribute__((__packed__)) {
  uint64_t len; // the length of data to be sent.
  char data[1]; // must be "struct hack".
} packet;

#endif
