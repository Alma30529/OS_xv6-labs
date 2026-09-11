#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define SEP " -\r\t\n./,"

int
is_sep(char c)
{
  return strchr(SEP, c) != 0;
}

int
is_digit(char c)
{
  return c >= '0' && c <= '9';
}

void
sixfive(int fd)
{
  char c;
  char buf[64];
  int len = 0;
  int valid = 1;
  int n;

  while (read(fd, &c, 1) == 1) {
    if (is_digit(c)) {
      if (len < (int)sizeof(buf) - 1)
        buf[len++] = c;
    } else {
      if (len > 0) {
        if (valid && is_sep(c)) {
          buf[len] = '\0';
          n = atoi(buf);
          if (n % 5 == 0 || n % 6 == 0)
            printf("%d\n", n);
        }
        len = 0;
      }
      valid = is_sep(c);
    }
  }

  if (len > 0 && valid) {
    buf[len] = '\0';
    n = atoi(buf);
    if (n % 5 == 0 || n % 6 == 0)
      printf("%d\n", n);
  }
}

int
main(int argc, char *argv[])
{
  int fd;

  if (argc <= 1) {
    sixfive(0);
    exit(0);
  }

  for (int i = 1; i < argc; i++) {
    if ((fd = open(argv[i], 0)) < 0) {
      fprintf(2, "sixfive: cannot open %s\n", argv[i]);
      exit(1);
    }
    sixfive(fd);
    close(fd);
  }

  exit(0);
}
