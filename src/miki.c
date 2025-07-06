#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <grp.h>
#include <pwd.h>
#include <err.h>
#include <glob.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define REQUEST 1028
#define BUFFER 65536

#ifndef __OpenBSD__
int pledge(const char *promises, const char *execpromises) {
  (void) promises;
  (void) execpromises;
  return 0;
}

int unveil(const char *path, const char *permissions) {
  (void) path;
  (void) permissions;
  return 0;
}
#endif

#include "../config.h"

const char *valid = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz0123456789"
                    " -._~:/?#[]@!$&'()*+,;=%\r\n";

struct request {
  int socket;
  time_t time;
};

void die(int eval, const char *msg) {
  syslog(LOG_ERR, "%s", msg);
  _exit(eval);
}

void deliver(int s, char *buf, int len) {
  while(len > 0) {
    ssize_t ret = write(s, buf, len);
    if(ret == -1) die(1, "write failed");
    buf += ret; len -= ret;
  }
}

void transfer(struct request *req, int fd) {
  char buf[BUFFER] = {0};
  ssize_t len;
  while((len = read(fd, buf, BUFFER)) > 0) {
    deliver(req->socket, buf, len);
  }
  if(len == -1) die(1, "read failed");
}

int daytime(time_t *t, float lat) {
  struct tm *tm = localtime(t);

  int day = tm->tm_yday + 1;
  float x = sin(360.0 * (day + 284) / 365.0 * M_PI / 180);
  float y = -tan(lat * M_PI / 180) * tan(23.44 * x * M_PI / 180);
  if(y < -1) y = -1;
  if(y > 1) y = 1;
  float hours = 1 / 15.0 * acos(y) * 180 / M_PI;

  struct tm *noon = localtime(t);
  noon->tm_hour = 12;
  noon->tm_min = 0;
  noon->tm_sec = 0;

  int delta_s = hours * 60 * 60;
  time_t noon_t = mktime(noon);

  return fabs(difftime(*t, noon_t)) <= delta_s;
}

int problem(struct request *req, char *msg) {
  deliver(req->socket, msg, strlen(msg));
  return 0;
}

int file(struct request *req, char *path) {
  int fd = open(path, O_RDONLY);
  if(fd == -1) return problem(req, "not found");
  transfer(req, fd);
  close(fd);
  return 0;
}

void entry(struct request *req, char *path) {
  struct stat sb = {0};
  stat(path, &sb);
  double size = sb.st_size / 1000.0;
  char buf[PATH_MAX * 2];
  int len = snprintf(buf, sizeof(buf), "=> %s [%.2f KB]\n", path, size);
  if(len >= (int)(sizeof(buf))) return;
  deliver(req->socket, buf, len);
}

int ls(struct request *req, char *path) {
  if(*path == '/') path++;

  if(*path && chdir(path)) return problem(req, "not found");

  struct stat sb = {0};
  stat("index.nex", &sb);
  if(S_ISREG(sb.st_mode))
    return file(req, "index.nex");

  glob_t res;
  if(glob("*", GLOB_MARK, 0, &res)) {
    char *empty = "(*^o^*)\r\n";
    deliver(req->socket, empty, strlen(empty));
    return 0;
  }
  for(size_t i = 0; i < res.gl_pathc; i++) {
    entry(req, res.gl_pathv[i]);
  }
  globfree(&res);
  return 0;
}

int miki(struct request *req, char *path) {
  size_t eof = strspn(path, valid);
  if(path[eof]) return problem(req, "we don't go there");

  req->time = time(0);
  if(nocturnal && daytime(&req->time, latitude)) {
    return file(req, "closed.nex");
  }

  path[strcspn(path, "\r\n")] = 0;
  if(!path || !*path) path = "/";

  if(path[strlen(path) - 1] == '/') {
    return ls(req, path);
  }

  if(*path == '/') path++;

  struct stat sb = {0};
  stat(path, &sb);

  return S_ISREG(sb.st_mode) ? file(req, path) : problem(req, "not found");
}

int main(int argc, char *argv[]) {
  int debug = 0;

  int c;
  while((c = getopt(argc, argv, "d")) != -1) {
    switch(c) {
      case 'd': debug = 1;
    }
  }

  tzset();

  struct sockaddr_in6 addr;
  int server = socket(AF_INET6, SOCK_STREAM, 0);

  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_loopback;
  addr.sin6_port = htons(1900);

  struct timeval timeout;
  timeout.tv_sec = 10;

  int opt = 1;
  setsockopt(server, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(server, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  if(bind(server, (struct sockaddr *) &addr, (socklen_t) sizeof(addr)))
    errx(1, "bind totally failed %d", errno);

  struct group *grp = {0};
  struct passwd *pwd = {0};

  if(group && !(grp = getgrnam(group)))
    errx(1, "group %s not found", group);

  if(user && !(pwd = getpwnam(user)))
    errx(1, "user %s not found", user);

  if(!debug)
    daemon(0, 0);

  if(unveil(root, "r")) errx(1, "unveil failed");
  if(chdir(root)) errx(1, "chdir failed");

  openlog("miki", LOG_NDELAY, LOG_DAEMON);

  if(group && grp && setgid(grp->gr_gid)) errx(1, "setgid failed");
  if(user && pwd && setuid(pwd->pw_uid)) errx(1, "setuid failed");

  if(pledge("stdio inet proc rpath", 0))
    errx(1, "pledge failed");

  listen(server, 32);

  signal(SIGCHLD, SIG_IGN);

  int sock;
  socklen_t len = sizeof(addr);
  while((sock = accept(server, (struct sockaddr *) &addr, &len)) > -1) {
    pid_t pid = fork();
    if(pid == -1) errx(1, "fork failed");
    if(!pid) {
      close(server);
      struct request req = {0};
      char path[REQUEST] = {0};
      ssize_t bytes = read(sock, path, REQUEST - 1);
      if(bytes <= 0) {
        close(sock);
        if(bytes == 0)
          errx(1, "disconnected");
        else
          errx(1, "read failed");
      }
      path[bytes] = '\0';

      char ip[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &addr, ip, INET6_ADDRSTRLEN);
      req.socket = sock;
      syslog(LOG_INFO, "%s %s", path, ip);
      miki(&req, path);
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
