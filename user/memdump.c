#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data, int len);

int
main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2026};
    memdump("ii", (char *)a, sizeof(a));

    printf("Example 2:\n");
    memdump("S", "a string", sizeof("a string"));

    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *)&s, sizeof(s));

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char *)&example, sizeof(example));

    printf("Example 5:\n");
    memdump("sccccc", (char *)&example, sizeof(example));
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data, n);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

static void
print_hex64(uint64 val)
{
  char digits[] = "0123456789ABCDEF";
  char buf[17];
  int i = 16;
  buf[16] = '\0';
  if (val == 0) {
    printf("0\n");
    return;
  }
  while (val > 0) {
    i--;
    buf[i] = digits[val & 0xf];
    val >>= 4;
  }
  printf("%s\n", buf + i);
}

void
memdump(char *fmt, char *data, int len)
{
  int pos = 0;
  int i;

  for (i = 0; fmt[i] != '\0'; i++) {
    char f = fmt[i];

    if (f == 'i') {
      if (pos + 4 > len) {
        printf("memdump: not enough data for '%c'\n", f);
        return;
      }
      int val;
      memmove(&val, data + pos, 4);
      printf("%d\n", val);
      pos += 4;

    } else if (f == 'p') {
      if (pos + 8 > len) {
        printf("memdump: not enough data for '%c'\n", f);
        return;
      }
      uint64 val;
      memmove(&val, data + pos, 8);
      print_hex64(val);
      pos += 8;

    } else if (f == 'h') {
      if (pos + 2 > len) {
        printf("memdump: not enough data for '%c'\n", f);
        return;
      }
      short val;
      memmove(&val, data + pos, 2);
      printf("%d\n", val);
      pos += 2;

    } else if (f == 'c') {
      if (pos + 1 > len) {
        printf("memdump: not enough data for '%c'\n", f);
        return;
      }
      printf("%c\n", data[pos]);
      pos += 1;

    } else if (f == 's') {
      if (pos + 8 > len) {
        printf("memdump: not enough data for '%c'\n", f);
        return;
      }
      char *ptr;
      memmove(&ptr, data + pos, 8);
      printf("%s\n", ptr);
      pos += 8;

    } else if (f == 'S') {
      int j = pos;
      while (j < len && data[j] != '\0') {
        printf("%c", data[j]);
        j++;
      }
      printf("\n");
      pos = len;
    }
  }
}
