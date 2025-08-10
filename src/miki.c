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
#define pledge(p, e) 0
#define unveil(p, r) 0
#endif

#include "../config.h"

const char *valid = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz0123456789"
                    " -._~:/?#[]@!$&'()*+,;=%\r\n";

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

int daytime(time_t *t, float lat) {
  struct tm *tm = localtime(t);
  int day = tm->tm_yday + 1;
  float x = sin(360.0 * (day + 284) / 365.0 * M_PI / 180);
  float y = -tan(lat * M_PI / 180) * tan(23.44 * x * M_PI / 180);
  if(y < -1) y = -1;
  if(y > 1) y = 1;
  float hours = 1 / 15.0 * acos(y) * 180 / M_PI;
  tm->tm_hour = 12, tm->tm_min = 0, tm->tm_sec = 0;
  int delta_s = hours * 60 * 60;
  time_t noon_t = mktime(tm);
  return fabs(difftime(*t, noon_t)) <= delta_s;
}

int problem(int socket, char *msg) {
  deliver(socket, msg, strlen(msg));
  return 0;
}

int file(int socket, char *path) {
  int fd = open(path, O_RDONLY);
  if(fd == -1) return problem(socket, notfound);
  char buf[BUFFER];
  ssize_t len;
  while((len = read(fd, buf, BUFFER)) > 0) deliver(socket, buf, len);
  if(len == -1) die(1, "read failed");
  close(fd);
  return 0;
}

int ls(int socket, char *path) {
  if(*path == '/') path++;
  if(*path && chdir(path)) return problem(socket, notfound);
  struct stat sb;
  if(!stat("index.nex", &sb) && S_ISREG(sb.st_mode))
    return file(socket, "index.nex");
  glob_t res;
  if(glob("*", GLOB_MARK, 0, &res)) {
    deliver(socket, empty, strlen(empty));
    return 0;
  }
  for(size_t i = 0; i < res.gl_pathc; i++) {
    deliver(socket, "=> ", 3);
    deliver(socket, res.gl_pathv[i], strlen(res.gl_pathv[i]));
    deliver(socket, "\n", 1);
  }
  globfree(&res);
  return 0;
}

int miki(int socket, char *path) {
  size_t bytes = strspn(path, valid);
  if(path[bytes]) return problem(socket, "we don't go there");
  path[strcspn(path, "\r\n")] = 0;
  if(!path || !*path) path = "/";
  time_t now = time(0);
  if(nocturnal && daytime(&now, latitude)) return file(socket, "closed.nex");
  if(path[strlen(path) - 1] == '/') return ls(socket, path);
  if(*path == '/') path++;
  struct stat sb;
  if(stat(path, &sb) == -1) return problem(socket, notfound);
  return S_ISREG(sb.st_mode) ? file(socket, path) : problem(socket, notfound);
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
  int server = socket(AF_INET6, SOCK_STREAM, 0);
  struct sockaddr_in6 addr = {
    .sin6_family = AF_INET6,
    .sin6_addr = in6addr_loopback,
    .sin6_port = htons(1900),
  };
  struct timeval timeout = {3, 0};
  int opt = 1;
  setsockopt(server, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(server, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  if(bind(server, (struct sockaddr *) &addr, (socklen_t) sizeof(addr)))
    errx(1, "bind totally failed");
  struct group *grp;
  struct passwd *pwd;
  if(group && !(grp = getgrnam(group))) errx(1, "group %s not found", group);
  if(user && !(pwd = getpwnam(user))) errx(1, "user %s not found", user);
  if(!debug) daemon(0, 0);
  if(unveil(root, "r")) errx(1, "unveil failed");
  if(chdir(root)) errx(1, "chdir failed");
  if(group && grp && setgid(grp->gr_gid)) errx(1, "setgid failed");
  if(user && pwd && setuid(pwd->pw_uid)) errx(1, "setuid failed");
  if(pledge("stdio inet proc rpath", 0)) errx(1, "pledge failed");
  openlog("miki", LOG_NDELAY, LOG_DAEMON);
  listen(server, 32);
  signal(SIGCHLD, SIG_IGN);
  socklen_t len = sizeof(addr);
  int sock;
  while((sock = accept(server, (struct sockaddr *) &addr, &len)) > -1) {
    pid_t pid = fork();
    if(pid == -1) errx(1, "fork failed");
    if(!pid) {
      close(server);
      char path[REQUEST];
      ssize_t bytes = read(sock, path, REQUEST - 1);
      if(bytes <= 0) {
        close(sock);
        errx(1, bytes == 0 ? "disconnected" : "read failed");
      }
      path[bytes] = '\0';
      char ip[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &addr, ip, INET6_ADDRSTRLEN);
      syslog(LOG_INFO, "%s %s", path, ip);
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
