#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <netdb.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <grp.h>
#include <pwd.h>
#include <err.h>
#include <glob.h>
#include <math.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#define REQUEST 1028
#define BUFFER 65536

const int backlog = 128;

const char *root = "/var/nex";

const char *user = "www";
const char *group = "www";

const char *addr = "::1";
const char *port = "1900";

const char *notfound = "not found";

int debug = 0;
int nocturnal = 1;
double latitude = 35.68;

const char *valid = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz0123456789"
                    " -._~:/?#[]@!$&'()*+,;=%\r\n";

const char *flags = "[-dh] [-u user] [-g group] [-a address] [-p port] "
                    "[-r root] [-l latitude]";

static void die(int eval, const char *msg) {
  syslog(LOG_ERR, "%s", msg);
  _exit(eval);
}

static void deliver(int server, const char *buf, ssize_t len) {
  while(len > 0) {
    ssize_t ret = write(server, buf, (size_t)len);
    if(ret == -1) die(1, "write failed");
    buf += ret; len -= ret;
  }
}

static int daytime(const time_t *t, double lat) {
  struct tm tms;
  struct tm *tm = localtime_r(t, &tms);
  if(!tm) die(1, "outside of time and space");

  int day = tm->tm_yday + 1;
  double x = sin(360.0 * (day + 284) / 365.0 * M_PI / 180);
  double y = -tan(lat * M_PI / 180) * tan(23.44 * x * M_PI / 180);
  if(y < -1) y = -1;
  if(y > 1) y = 1;
  double hours = 1 / 15.0 * acos(y) * 180 / M_PI;
  tm->tm_hour = 12, tm->tm_min = 0, tm->tm_sec = 0;
  int delta_s = (int)hours * 60 * 60;
  time_t noon_t = mktime(tm);
  return fabs(difftime(*t, noon_t)) <= delta_s;
}

static int problem(int socket, const char *msg) {
  deliver(socket, msg, (ssize_t)strlen(msg));
  return 0;
}

static int file(int socket, const char *path) {
  int fd = open(path, O_RDONLY);
  if(fd == -1) return problem(socket, notfound);
  char buf[BUFFER];
  ssize_t ret;
  while((ret = read(fd, buf, BUFFER)) > 0)
    deliver(socket, buf, (ssize_t)ret);
  if(ret == -1) die(1, "read failed");
  close(fd);
  return 0;
}

static int ls(int socket, const char *path) {
  if(*path == '/') path++;
  if(*path && chdir(path)) return problem(socket, notfound);
  struct stat sb;
  if(!stat("index.nex", &sb) && S_ISREG(sb.st_mode))
    return file(socket, "index.nex");
  glob_t res;
  if(glob("*", GLOB_MARK, 0, &res)) {
    return problem(socket, notfound);
  }
  for(size_t i = 0; i < res.gl_pathc; i++) {
    deliver(socket, "=> ", 3);
    deliver(socket, res.gl_pathv[i], (ssize_t)strlen(res.gl_pathv[i]));
    deliver(socket, "\n", 1);
  }
  globfree(&res);
  return 0;
}

static double mustdouble(const char *raw, const char *errmsg) {
  char *end;
  double value = strtod(raw, &end);
  if(end == raw) errx(1, "%s", errmsg);
  return value;
}

static int miki(int socket, char *path) {
  size_t bytes = strspn(path, valid);
  if(path[bytes]) return problem(socket, "we don't go there");
  path[strcspn(path, "\r\n")] = 0;
  if(!*path) strlcpy(path, "/", REQUEST);
  time_t now = time(0);
  if(nocturnal && daytime(&now, latitude)) return file(socket, "closed.nex");
  if(path[strlen(path) - 1] == '/') return ls(socket, path);
  if(*path == '/') path++;
  struct stat sb;
  if(stat(path, &sb) == -1) return problem(socket, notfound);
  return S_ISREG(sb.st_mode) ? file(socket, path) : problem(socket, notfound);
}

static void usage(const char *name) {
  fprintf(stderr, "usage: %s %s\n", name, flags);
}

static void help(const char *name) {
  usage(name);

  fprintf(stderr, "-d - debug mode - don't daemonize\n");
  fprintf(stderr, "-u user - setuid to user\n");
  fprintf(stderr, "-g group - setgid to group\n");
  fprintf(stderr, "-a address - listen on address\n");
  fprintf(stderr, "-a port - listen on port\n");
  fprintf(stderr, "-r root - nex root\n");
}

int main(int argc, char *argv[]) {
  int c;
  while((c = getopt(argc, argv, "dhu:g:a:p:r:l:")) != -1) {
    switch(c) {
      case 'd': debug = 1; break;
      case 'u': user = optarg; break;
      case 'g': group = optarg; break;
      case 'a': addr = optarg; break;
      case 'p': port = optarg; break;
      case 'r': root = optarg; break;
      case 'l': latitude = mustdouble(optarg, "invalid latitude"); break;
      case 'h': help(argv[0]); exit(0);
      default: usage(argv[0]); exit(0);
    }
  }
  if(!strtonum(port, 1, 65535, 0)) errx(1, "invalid port");

  tzset();

  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  int err = getaddrinfo(addr, port, &hints, &res);
  if(err)
    errx(1, "getaddrinfo: %s", gai_strerror(err));

  int server = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if(server == -1)
    errx(1, "socket failed");

  struct timeval tv = {.tv_sec = 10};

  int opt = 1;
  if(setsockopt(server, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
    errx(1, "setsockopt TCP_NODELAY failed");
  if(setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    errx(1, "setsockopt SO_REUSEADDR failed");
  if(setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
    errx(1, "setsockopt SO_RCVTIMEO failed");
  if(setsockopt(server, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1)
    errx(1, "setsockopt SO_SNDTIMEO failed");

  if(bind(server, res->ai_addr, res->ai_addrlen))
    errx(1, "bind failed: %s", strerror(errno));

  freeaddrinfo(res);

  const struct group *grp = {0};
  const struct passwd *pwd = {0};

  if(group && !(grp = getgrnam(group)))
    errx(1, "group %s not found", group);

  if(user && !(pwd = getpwnam(user)))
    errx(1, "user %s not found", user);

  if(chdir(root)) errx(1, "chdir failed");

  openlog("miki", LOG_NDELAY, LOG_DAEMON);

  if(group && grp && setgid(grp->gr_gid)) errx(1, "setgid failed");
  if(user && pwd && setuid(pwd->pw_uid)) errx(1, "setuid failed");

#ifdef __OpenBSD__
  if(!debug) daemon(1, 0);
  if(unveil(root, "r")) errx(1, "unveil failed");
  if(pledge("stdio inet proc rpath", 0))
    errx(1, "pledge failed");
#endif

  if(listen(server, backlog)) errx(1, "listen failed");

  signal(SIGCHLD, SIG_IGN);

  struct sockaddr_storage client;
  socklen_t len = sizeof(client);

  int sock;
  while((sock = accept(server, (struct sockaddr *) &client, &len)) > -1) {
    pid_t pid = fork();
    if(pid == -1) errx(1, "fork failed");
    if(!pid) {
      close(server);
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

      char path[REQUEST];
      ssize_t bytes = read(sock, path, REQUEST - 1);
      if(bytes <= 0) {
        close(sock);
        errx(1, bytes == 0 ? "disconnected" : "read failed");
      }
      path[bytes] = '\0';

      miki(sock, path);
      close(sock);
      _exit(0);
    } else {
      close(sock);
    }
  }
  close(server);
  closelog();
  return 0;
}
