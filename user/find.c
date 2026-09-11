#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  memmove(buf, p, strlen(p));
  buf[strlen(p)] = 0;

  return buf;
}

void
run_exec(char *argv[], int argc, char *matched_path)
{
  char *execargv[MAXARG];
  int i;

  for (i = 0; i < argc; i++)
    execargv[i] = argv[i];
  execargv[i++] = matched_path;
  execargv[i] = 0;

  if (fork() == 0) {
    exec(execargv[0], execargv);
    fprintf(2, "find: exec %s failed\n", execargv[0]);
    exit(1);
  } else {
    wait(0);
  }
}

void
find(char *path, char *target, char *execargv[], int execargc)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type) {
  case T_FILE:
    if (strcmp(fmtname(path), target) == 0) {
      if (execargc > 0)
        run_exec(execargv, execargc, path);
      else
        printf("%s\n", path);
    }
    break;

  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0)
        continue;
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      if (stat(buf, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", buf);
        continue;
      }

      if (strcmp(de.name, target) == 0) {
        if (execargc > 0)
          run_exec(execargv, execargc, buf);
        else
          printf("%s\n", buf);
      }

      if (st.type == T_DIR)
        find(buf, target, execargv, execargc);
    }
    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  char *execargv[MAXARG];
  int execargc = 0;

  if (argc < 3) {
    fprintf(2, "Usage: find <directory> <name> [-exec cmd ...]\n");
    exit(1);
  }

  if (argc > 3) {
    if (strcmp(argv[3], "-exec") != 0) {
      fprintf(2, "Usage: find <directory> <name> [-exec cmd ...]\n");
      exit(1);
    }
    for (int i = 4; i < argc; i++)
      execargv[execargc++] = argv[i];
  }

  find(argv[1], argv[2], execargv, execargc);
  exit(0);
}
