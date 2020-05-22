/*
* Header only functions
*/

#ifndef AVZB_H
#define AVZB_H
#include <instant.h>

extern uint16_t PORT;

static bool str_to_uint16(const char *str, uint16_t *res)
{
  char *end;
  errno = 0;
  intmax_t val = strtoimax(str, &end, 10);
  if (errno == ERANGE || val < 0 || val > UINT16_MAX || end == str ||
      *end != '\0')
    return false;
  *res = (uint16_t)val;
  return true;
}

int argparser(int argcc, char *argvv[], char *IP_ADDD, void *PORTX)
{
  if (argcc < 2)
  {
    printf("Argument is too few. Exiting...\n");
    return 1;
  }
  if (argcc == 2)
  {
    printf("Port is not defined, using default port %hu\n", PORT);
    return 0;
  }
  if (argcc == 3)
  {
    str_to_uint16(argvv[2], PORTX);
    strncpy(IP_ADDD, argvv[1], strlen(argvv[1]));
    return 0;
  }
  return 0;
}

uint64_t findfileSize(char f_n[])
{
  FILE *fp = fopen(f_n, "r"); // opening a file in read mode
  if (fp == NULL)             // checking whether the file exists or not
  {
    printf("File Not Found!\n");
    return -1;
  }
  fseek(fp, 0L, SEEK_END);
  uint64_t res = ftell(fp); //counting the size of the file
  fclose(fp);               //closing the file
  return res;
}

#endif