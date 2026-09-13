#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  char buf[1];
  printf("entering raw mode, type a few keys then Enter:\n");
  rawmode(1);
  for (int i = 0; i < 10; i++) {
    int n = read(0, buf, 1);
    if (n <= 0) break;
    printf("got byte: %d ('%c')\n", buf[0], buf[0] >= 32 && buf[0] < 127 ? buf[0] : '?');
    if (buf[0] == '\r' || buf[0] == '\n') break;
  }
  rawmode(0);
  printf("back to cooked mode\n");
  exit(0);
}
