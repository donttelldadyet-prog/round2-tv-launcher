/* Round2 PS4: a name lookup that can never hang (helper 1.1.26, 16 Nov).
   Why: connect, send and read have had time limits since 1.1.15, but the name lookup (getaddrinfo) had none. Bella's helper
   stood still for an hour on weak Wi-Fi (16 Nov 8:20pm–9:27pm) while the rest of the PS4 ran – the lookup was the only step
   left without a limit. This asks the DNS servers itself over UDP and gives up after R2DNS_WAIT_MS per server.
   Plain POSIX sockets, so it is tested on a computer too (tls/r2dns_test.c). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "r2tls.h"

#define R2DNS_WAIT_MS 2500
static char dns_srv[4][20];
static int dns_port = 53; /* only the test changes it */
void r2dns_set_port(int p) { dns_port = p; }
void r2tls_set_dns(const char *a, const char *b) { /* the PS4's own DNS servers (network settings); asked first */
  snprintf(dns_srv[0], sizeof dns_srv[0], "%s", a ? a : "");
  snprintf(dns_srv[1], sizeof dns_srv[1], "%s", b ? b : "");
}

/* one question to one server; 0 = got an address (dotted, in out), -1 = nothing usable */
static int ask(const char *server, const char *host, char *out, size_t outn) {
  struct sockaddr_in to; memset(&to, 0, sizeof to);
  to.sin_family = AF_INET; to.sin_port = htons(dns_port);
  if (!server[0] || inet_pton(AF_INET, server, &to.sin_addr) != 1) return -1;
  unsigned char q[300], r[600];
  size_t hl = strlen(host), n = 12;
  if (hl < 1 || hl > 250) return -1;
  static unsigned short seq;
  unsigned short id = (unsigned short)(0x5232 + (++seq) * 7919u + (unsigned)getpid());
  memset(q, 0, 12); q[0] = id >> 8; q[1] = id & 255; q[2] = 1; q[5] = 1; /* recursion wanted, 1 question */
  for (const char *p = host; *p;) { /* labels */
    const char *d = strchr(p, '.'); size_t l = d ? (size_t)(d - p) : strlen(p);
    if (l < 1 || l > 63) return -1;
    q[n++] = (unsigned char)l; memcpy(q + n, p, l); n += l;
    p += l; if (*p == '.') p++;
  }
  q[n++] = 0; q[n++] = 0; q[n++] = 1; q[n++] = 0; q[n++] = 1; /* type A, class IN */
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return -1;
  int ok = -1;
  struct timeval st = { 2, 0 }; setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &st, sizeof st);
  if (sendto(fd, q, n, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)n) {
    struct timeval end, now; gettimeofday(&end, 0);
    long left = R2DNS_WAIT_MS;
    while (left > 0 && ok) {
      fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
      struct timeval tv = { left / 1000, (left % 1000) * 1000 };
      if (select(fd + 1, &rf, 0, 0, &tv) <= 0) break;
      ssize_t g = recv(fd, r, sizeof r, MSG_DONTWAIT);
      gettimeofday(&now, 0);
      left = R2DNS_WAIT_MS - ((now.tv_sec - end.tv_sec) * 1000 + (now.tv_usec - end.tv_usec) / 1000);
      if (g < 12 || r[0] != q[0] || r[1] != q[1] || !(r[2] & 0x80)) continue; /* not our answer */
      if (r[3] & 15) break; /* the server says no (e.g. no such name) */
      size_t p = 12; int qd = (r[4] << 8) | r[5], an = (r[6] << 8) | r[7];
      #define SKIPNAME() do { while (p < (size_t)g) { unsigned char c = r[p]; if (!c) { p++; break; } if ((c & 0xc0) == 0xc0) { p += 2; break; } p += 1 + c; } } while (0)
      while (qd-- > 0 && p < (size_t)g) { SKIPNAME(); p += 4; }
      while (an-- > 0 && p + 10 <= (size_t)g) {
        SKIPNAME();
        if (p + 10 > (size_t)g) break;
        int type = (r[p] << 8) | r[p + 1], rl = (r[p + 8] << 8) | r[p + 9];
        p += 10;
        if (p + rl > (size_t)g) break;
        if (type == 1 && rl == 4 && (r[p] | r[p + 1] | r[p + 2] | r[p + 3])) { /* 0.0.0.0 = blocked, not an address */
          snprintf(out, outn, "%u.%u.%u.%u", r[p], r[p + 1], r[p + 2], r[p + 3]); ok = 0; break;
        }
        p += rl;
      }
      break; /* our answer was read, with or without an address */
    }
  }
  close(fd);
  return ok;
}

/* host -> dotted address. Tries the PS4's own servers, then the blocker on this PS4, then a public one – at most about
   10 s in all, usually a few ms. 0 = found. An address typed as numbers comes straight back. */
int r2dns_lookup(const char *host, char *out, size_t outn) {
  struct in_addr t;
  if (inet_pton(AF_INET, host, &t) == 1) { snprintf(out, outn, "%s", host); return 0; }
  snprintf(dns_srv[2], sizeof dns_srv[2], "127.0.0.1");
  snprintf(dns_srv[3], sizeof dns_srv[3], "1.1.1.1");
  for (int i = 0; i < 4; i++) {
    int dup = 0; for (int j = 0; j < i; j++) if (!strcmp(dns_srv[i], dns_srv[j])) dup = 1;
    if (!dup && !ask(dns_srv[i], host, out, outn)) return 0;
  }
  return -1;
}

int r2dns_lookup_own(const char *host, char *out, size_t outn) { /* only the PS4's own servers (is a name blocked here?) */
  for (int i = 0; i < 2; i++) if ((i == 0 || strcmp(dns_srv[1], dns_srv[0])) && !ask(dns_srv[i], host, out, outn)) return 0;
  return -1;
}

/* ---- "where am I" notes: which step the helper is in, and a line in slow.log for any step that took over a minute ----
   So the next standstill names its own cause instead of leaving us guessing (16 Nov: three theories in one evening). */
static char w_name[48] = "start"; static long long w_wall; static double w_mono; static int w_save, w_written;
static double mono_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static void stamp(char *b, size_t n, long long t) { time_t x = (time_t)t + 8 * 3600; struct tm tm; gmtime_r(&x, &tm); snprintf(b, n, "%02d/%02d %02d:%02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec); }
void r2_where(const char *name, int save) {
  long long wall = (long long)time(0); double mono = mono_now();
  if (w_wall && (wall - w_wall > 60 || mono - w_mono > 60) && strcmp(w_name, "wait")) { /* "wait" = the 2 s nap; rest mode shows there and is reported as a wake already */
    FILE *f = fopen("/data/round2/slow.log", "a");
    if (f) { char s[24]; stamp(s, sizeof s, w_wall); fprintf(f, "%s step '%s' took %lld s (running clock %ld s)\n", s, w_name, wall - w_wall, (long)(mono - w_mono)); fclose(f); }
  }
  snprintf(w_name, sizeof w_name, "%s", name); w_wall = wall; w_mono = mono;
  /* 1.1.71 (slimming): where.txt is no longer written at every step (~8,600 disk writes a day). The watchdog writes it once a
     step has lasted 10 s (the only case it is needed for: a freeze), and on the way out (watchdog restart, stop signal). */
  w_save = save; w_written = 0;
}
void r2_where_flush(double min_age) {
  if (!w_save || w_written || !w_mono || mono_now() - w_mono < min_age) return;
  w_written = 1;
  FILE *f = fopen("/data/round2/where.txt", "w"); if (f) { char s[24]; stamp(s, sizeof s, w_wall); fprintf(f, "%s %s\n", s, w_name); fclose(f); }
}
double r2_where_age(void) { return w_mono ? mono_now() - w_mono : 0; }   /* 1.1.42: for the watchdog */
/* 1.1.70 (Bella 19 Nov): the running clock does NOT stand still in rest mode (Bella 44261 s, Atlas 23491 s, Kaeden 810 s after
   a sleep) – the watchdog moves the step's start on by the time the PS4 slept, so a wake never looks like a freeze. */
void r2_where_shift(double secs) { if (w_mono) w_mono += secs; }
const char *r2_where_now(void) { return w_name; }
void r2_where_start(const char *version) { /* at start: what was the last run doing when it ended? */
  char b[100] = ""; FILE *f = fopen("/data/round2/where.txt", "r");
  if (f) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); b[strcspn(b, "\r\n")] = 0; }
  f = fopen("/data/round2/slow.log", "a");
  if (f) { char s[24]; stamp(s, sizeof s, (long long)time(0)); fprintf(f, "%s helper %s started; the run before was last in: %s\n", s, version, b[0] ? b : "(no note)"); fclose(f); }
  r2_where("start", 1);
}

/* 1.1.27 (16 Nov, owner: "what's the next step"): a line in /data/round2/net-fail.log for every FAILED request to the
   engine (time, request, the step it failed in, the error) and one line when they work again ("back after N failures,
   M s"). 1.1.26 made the steps give up quickly instead of hanging – so a dead spell now shows up as failures, not as a
   slow step, and this is where it is written down. Kept under 32 KB (old file -> net-fail.log.old). */
#define R2_HOST "round2-book.donttelldadyet.workers.dev"
__attribute__((weak)) void r2_bpop(const char *key, const char *msg) { (void)key; (void)msg; } /* 1.1.75: helper pop-ups */
static int nf_count; static long long nf_first;
/* 1.1.72 (Bella 19 Nov: on, its helper running, other PS4s on the shop network could see it, yet every connection to the
   engine failed for 2 h 14 min): when the engine can't be reached, test the path piece by piece and write it down –
   the shop router, an internet address that is not Cloudflare, and the engine's last known address. */
static const char *probe(const char *ip, int port) {
  struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)port);
  if (!ip || inet_pton(AF_INET, ip, &sa.sin_addr) != 1) return "no address";
  int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) return "no socket";
  int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  const char *r = "ok";
  if (connect(fd, (struct sockaddr *)&sa, sizeof sa)) {
    if (errno != EINPROGRESS) r = errno == ECONNREFUSED ? "refused (reachable)" : "error";
    else { fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf); struct timeval tv = { 3, 0 }; int e = 0; socklen_t el = sizeof e;
      if (select(fd + 1, 0, &wf, 0, &tv) <= 0) r = "no answer in 3 s";
      else if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el) || e) r = e == ECONNREFUSED ? "refused (reachable)" : "failed"; }
  }
  close(fd); return r;
}
static void net_probe(FILE *f, const char *s) {
  char eng[64] = ""; FILE *c = fopen("/data/round2/addr-" R2_HOST ".txt", "r");
  if (c) { if (!fgets(eng, sizeof eng, c)) eng[0] = 0; fclose(c); eng[strcspn(eng, "\r\n")] = 0; }
  const char *pr = probe("192.168.88.1", 80), *pi = probe("1.1.1.1", 443), *pe = eng[0] ? probe(eng, 443) : "-";
  fprintf(f, "%s   path check: shop router %s · internet (1.1.1.1) %s · engine %s %s\n", s, pr, pi, eng[0] ? eng : "?", pe);
  { char m[200]; snprintf(m, sizeof m, "Path check: router %s, internet %s, Round2 %s", pr, pi, pe); r2_bpop("path", m); }
}
void r2_netnote(const char *path, int status, const char *err) {
  int ok = status >= 200 && status < 500;
  if (ok && !nf_count) return;
  FILE *f;
  { FILE *t = fopen("/data/round2/net-fail.log", "r"); if (t) { fseek(t, 0, SEEK_END); long sz = ftell(t); fclose(t); if (sz > 32768) rename("/data/round2/net-fail.log", "/data/round2/net-fail.log.old"); } }
  f = fopen("/data/round2/net-fail.log", "a");
  long long now = (long long)time(0); char s[24]; stamp(s, sizeof s, now);
  if (ok) {
    if (f) fprintf(f, "%s back after %d failure(s), %lld s\n", s, nf_count, now - nf_first);
    { char m[120]; snprintf(m, sizeof m, "Back online after %d failed tries (%lld s)", nf_count, now - nf_first); r2_bpop("net", m); }
    nf_count = 0;
  } else {
    if (!nf_count) nf_first = now;
    nf_count++;
    if (f && (nf_count <= 5 || nf_count % 10 == 0)) fprintf(f, "%s fail #%d %s in '%s': %d %.80s\n", s, nf_count, path ? path : "", w_name, status, err ? err : "");
    if (f && (nf_count == 3 || nf_count % 10 == 0)) net_probe(f, s);
    { char m[160]; snprintf(m, sizeof m, "Can't reach Round2 (try #%d, step '%s') - will keep trying", nf_count, w_name); r2_bpop("net", m); }
  }
  if (f) fclose(f);
}
