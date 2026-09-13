// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

#define MAXBG 64
int bgpids[MAXBG];
int nbg = 0;

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
void runcmd(struct cmd *) __attribute__((noreturn));

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0) {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

// Find the start of the current word being typed (last whitespace before buf[i], or buf itself)
int
wordstart(char *buf, int i)
{
  int s = i;
  while (s > 0 && buf[s - 1] != ' ' && buf[s - 1] != '\t')
    s--;
  return s;
}

#define MAXHIST  16
#define HISTLINE 100

char history[MAXHIST][HISTLINE];
int nhist = 0;

// Save a completed command line (buf[0..len)) into history.
void
addhistory(char *buf, int len)
{
  if (len <= 0)
    return;
  if (nhist == MAXHIST) {
    // drop the oldest entry, shift everything down
    for (int k = 1; k < MAXHIST; k++)
      memmove(history[k - 1], history[k], HISTLINE);
    nhist--;
  }
  memmove(history[nhist], buf, len);
  history[nhist][len] = 0;
  nhist++;
}

// Erase n characters that are currently displayed on screen.
void
eraseline(int n)
{
  for (int k = 0; k < n; k++)
    write(1, "\b \b", 3);
}

// Attempt tab completion on buf[0..i). Returns new i (cursor/length).
// interactive controls whether we echo characters we add.
int
tabcomplete(char *buf, int i, int interactive)
{
  int ws = wordstart(buf, i);
  int wlen = i - ws;
  char *word = buf + ws;

  int fd = open(".", O_RDONLY);
  if (fd < 0)
    return i;

  struct dirent {
    ushort inum;
    char name[14];
  } de;

  char common[14];
  int ncommon = -1; // -1 means "not yet set"
  int nmatches = 0;

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;
    // name field isn't guaranteed NUL-terminated if it's exactly 14 chars
    char name[15];
    memmove(name, de.name, 14);
    name[14] = 0;
    int namelen = strlen(name);

    if (namelen < wlen)
      continue;
    int match = 1;
    for (int k = 0; k < wlen; k++) {
      if (name[k] != word[k]) {
        match = 0;
        break;
      }
    }
    if (!match)
      continue;
    nmatches++;
    if (ncommon == -1) {
      strcpy(common, name);
      ncommon = namelen;
    } else {
      int k = 0;
      while (k < ncommon && k < namelen && common[k] == name[k])
        k++;
      ncommon = k;
    }
  }
  close(fd);

  if (nmatches == 0)
    return i;

  // Append the extra characters from wlen..ncommon of the common prefix
  int j = wlen;
  while (j < ncommon && i < 98) {
    buf[i] = common[j];
    if (interactive)
      write(1, &buf[i], 1);
    i++;
    j++;
  }
  return i;
}

int
getcmd(char *buf, int nbuf)
{
  struct stat st;
  int interactive;
  int i = 0;
  char c;
  int histidx;

  interactive = (fstat(0, &st) < 0 || st.type != T_FILE);

  if (interactive)
    write(2, "$ ", 2);

  memset(buf, 0, nbuf);
  histidx = nhist;

  if (interactive)
    rawmode(1);

  while (i < nbuf - 2) {
    int n = read(0, &c, 1);
    if (n <= 0)
      break; // EOF or error

    if (c == '\r' || c == '\n') {
      break;
    } else if (c == 127 || c == 8) {
      // backspace/delete
      if (i > 0) {
        i--;
        if (interactive)
          write(1, "\b \b", 3);
      }
    } else if (c == 9) {
      i = tabcomplete(buf, i, interactive);
      continue;
    } else if (c == 27) {
      char seq[2];
      if (read(0, &seq[0], 1) <= 0)
        break;
      if (seq[0] == '[') {
        if (read(0, &seq[1], 1) <= 0)
          break;
        if (seq[1] == 'A') {
          // Up arrow: recall older history entry
          if (histidx > 0) {
            histidx--;
            if (interactive)
              eraseline(i);
            i = strlen(history[histidx]);
            memmove(buf, history[histidx], i);
            if (interactive)
              write(1, buf, i);
          }
        } else if (seq[1] == 'B') {
          // Down arrow: move toward newer entry, or clear if already newest
          if (histidx < nhist) {
            histidx++;
            if (interactive)
              eraseline(i);
            if (histidx == nhist) {
              i = 0;
            } else {
              i = strlen(history[histidx]);
              memmove(buf, history[histidx], i);
              if (interactive)
                write(1, buf, i);
            }
          }
        }
        // 'C' (right) and 'D' (left) intentionally left as no-ops
      }
      continue;
    } else {
      buf[i++] = c;
      if (interactive)
        write(1, &c, 1);
    }
  }

  if (interactive) {
    write(1, "\n", 1);
    rawmode(0);
  }

  if (i > 0)
    addhistory(buf, i);

  buf[i] = '\n';
  buf[i + 1] = 0;

  if (i == 0 && buf[0] == '\n' && !interactive)
    return -1; // EOF on a script/pipe with nothing left to read

  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0) {
    char *cmd = buf;
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == '\n') // is a blank command
      continue;
    if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
      // Chdir must be called by the parent, not the child.
      cmd[strlen(cmd) - 1] = 0; // chop \n
      if (chdir(cmd + 3) < 0)
        fprintf(2, "cannot cd %s\n", cmd + 3);
    } else if (cmd[0] == 'w' && cmd[1] == 'a' && cmd[2] == 'i' &&
               cmd[3] == 't' && (cmd[4] == '\n' || cmd[4] == ' ')) {
      // wait builtin: wait for all tracked background jobs.
      if (nbg == 0) {
        fprintf(2, "wait: no background jobs\n");
      } else {
        while (nbg > 0) {
          wait(0);
          nbg--;
        }
      }
    } else {
      struct cmd *parsed = parsecmd(cmd);
      if (parsed->type == BACK) {
        struct backcmd *bcmd = (struct backcmd *)parsed;
        int pid = fork1();
        if (pid == 0) {
          runcmd(bcmd->cmd);
        } else if (nbg < MAXBG) {
          bgpids[nbg++] = pid;
        }
      } else {
        if (fork1() == 0)
          runcmd(parsed);
        wait(0);
      }
    }
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
