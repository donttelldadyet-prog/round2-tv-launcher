/* Round2 PS4 starter (round2-boot.elf) – the only Round2 file in GoldHEN AutoRun. It never changes, so it never needs
   copying again; the helper it starts updates itself through it.

   What it does (every 5 s, forever):
   - Keeps the current helper (/data/round2/app/current.elf) running. It starts it through GoldHEN's own payload loader
     (BinLoader, 127.0.0.1:9090 – the same as sending a payload from a computer). "Running" = the helper holds
     /data/round2/helper.lock (a crashed helper drops it).
   - Swaps in a new version: the helper downloads it (engine, SHA-256 checked), saves new.elf, writes "switch" and exits;
     the starter keeps the old one as previous.elf, moves new.elf to current.elf and starts it.
   - Trial: the new helper must say hello (writes its version to app/hello) within 3 minutes and not stop twice, otherwise
     the starter stops it, goes back to previous.elf and writes rolled-back.txt (the helper reports it; the control centre
     shows "rolled back").
   - First start: copies /data/payloads/round2d.elf to current.elf.
   Log: /data/round2/app/boot.log. Never touches games, saves, nanoDNS or GoldHEN's files. */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BOOT_VERSION "1"
#define APP "/data/round2/app"
#define CUR APP "/current.elf"
#define PREV APP "/previous.elf"
#define NEWF APP "/new.elf"
#define BAD APP "/bad.elf"
#define SWITCHF APP "/switch"
#define TRIALF APP "/trial"
#define HELLO APP "/hello"
#define STOPF APP "/stop"
#define ROLLED APP "/rolled-back.txt"
#define BOOTVER APP "/boot.ver"
#define LOGF APP "/boot.log"
#define LOCKF "/data/round2/helper.lock"
#define FIRST "/data/payloads/round2d.elf"
#define TRIAL_S 180

typedef struct notify_request { char useless1[45]; char message[3075]; } notify_request_t; /* same as the helper (proven) */
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
static void notify(const char *msg) {
  notify_request_t req;
  memset(&req, 0, sizeof req);
  strncpy(req.message, msg, sizeof req.message - 1);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

static double mono(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}
static void blog(const char *fmt, ...) {
  struct stat st;
  if (!stat(LOGF, &st) && st.st_size > 65536) rename(LOGF, APP "/boot.log.old");
  FILE *f = fopen(LOGF, "a");
  if (!f) return;
  time_t t = time(0) + 8 * 3600; /* PS4 clock, display only */
  struct tm tm;
  gmtime_r(&t, &tm);
  fprintf(f, "%02d/%02d %02d:%02d:%02d ", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}
static int exists(const char *p) { struct stat st; return !stat(p, &st); }
static void put(const char *p, const char *txt) {
  FILE *f = fopen(p, "w");
  if (f) { fputs(txt, f); fclose(f); }
}
static void get(const char *p, char *out, size_t n) {
  out[0] = 0;
  FILE *f = fopen(p, "r");
  if (!f) return;
  if (!fgets(out, n, f)) out[0] = 0;
  fclose(f);
  out[strcspn(out, "\r\n")] = 0;
}
static int copy_file(const char *from, const char *to) {
  FILE *a = fopen(from, "rb");
  if (!a) return -1;
  char tmp[300];
  snprintf(tmp, sizeof tmp, "%s.tmp", to);
  FILE *b = fopen(tmp, "wb");
  if (!b) { fclose(a); return -1; }
  char buf[65536];
  size_t n;
  int ok = 1;
  while ((n = fread(buf, 1, sizeof buf, a)) > 0) if (fwrite(buf, 1, n, b) != n) { ok = 0; break; }
  fclose(a);
  if (fclose(b) || !ok) { unlink(tmp); return -1; }
  return rename(tmp, to);
}

static int helper_running(void) { /* the helper holds the lock while it runs */
  int fd = open(LOCKF, O_RDWR | O_CREAT, 0666);
  if (fd < 0) return 0;
  int free_ = flock(fd, LOCK_EX | LOCK_NB) == 0;
  if (free_) flock(fd, LOCK_UN);
  close(fd);
  return !free_;
}

static int launch(const char *path) { /* hand the ELF to GoldHEN's BinLoader on this PS4 */
  FILE *f = fopen(path, "rb");
  if (!f) { blog("launch: can't open %s", path); return -1; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 64 || n > 16 * 1024 * 1024) { fclose(f); blog("launch: %s has a bad size (%ld)", path, n); return -1; }
  unsigned char *b = malloc(n);
  if (!b || fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); blog("launch: read failed"); return -1; }
  fclose(f);
  if (memcmp(b, "\x7f" "ELF", 4)) { free(b); blog("launch: %s is not an ELF", path); return -1; }
  int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(9090);
  a.sin_addr.s_addr = htonl(0x7f000001);
  if (s < 0 || connect(s, (struct sockaddr *)&a, sizeof a)) {
    if (s >= 0) close(s);
    free(b);
    blog("launch: BinLoader (port 9090) not answering, errno %d", errno);
    return -2;
  }
  long sent = 0;
  while (sent < n) {
    ssize_t k = send(s, b + sent, n - sent, 0);
    if (k <= 0) break;
    sent += k;
  }
  close(s);
  free(b);
  if (sent != n) { blog("launch: sent %ld of %ld bytes", sent, n); return -1; }
  blog("launched %s (%ld bytes)", path, n);
  return 0;
}

static char trial_v[40];
static double trial_t;
static int crashes;

static void start_and_wait(const char *why) {
  static int binwarn;
  blog("starting the helper (%s)", why);
  int r = launch(CUR);
  if (r == -2 && !binwarn++) notify("Round2 starter: GoldHEN BinLoader is off. Turn on GoldHEN > Server Settings > BinLoader.");
  for (int i = 0; i < 10 && !helper_running(); i++) sleep(2); /* give it 20 s to take the lock */
}

static void stop_helper(void) {
  put(STOPF, "stop");
  for (int i = 0; i < 15 && helper_running(); i++) sleep(2);
  unlink(STOPF);
}

static void rollback(const char *why) {
  char msg[200];
  blog("ROLLBACK from %s: %s", trial_v, why);
  stop_helper();
  if (exists(PREV)) {
    unlink(BAD);
    rename(CUR, BAD);
    rename(PREV, CUR);
    snprintf(msg, sizeof msg, "rolled back from %s: %s", trial_v, why);
    put(ROLLED, msg);
    snprintf(msg, sizeof msg, "Round2 helper %s didn't work (%s) - went back to the previous version.", trial_v, why);
    notify(msg);
  }
  unlink(TRIALF);
  trial_v[0] = 0;
  crashes = 0;
  start_and_wait("after roll-back");
}

static void swap_in(void) {
  char v[40];
  get(SWITCHF, v, sizeof v);
  if (!exists(NEWF)) { blog("switch to %s asked, but new.elf is missing", v); unlink(SWITCHF); return; }
  unlink(PREV);
  rename(CUR, PREV);
  if (rename(NEWF, CUR)) { rename(PREV, CUR); blog("switch failed (rename)"); unlink(SWITCHF); return; }
  snprintf(trial_v, sizeof trial_v, "%s", v[0] ? v : "?");
  put(TRIALF, trial_v);
  unlink(HELLO);
  unlink(SWITCHF);
  trial_t = mono();
  crashes = 0;
  blog("switched to %s (previous kept)", trial_v);
  start_and_wait("new version");
}

int main(void) {
  mkdir("/data/round2", 0777);
  mkdir(APP, 0777);
  put(BOOTVER, BOOT_VERSION);
  unlink(STOPF);
  blog("starter %s started", BOOT_VERSION);
  if (!exists(CUR)) {
    if (copy_file(FIRST, CUR)) {
      blog("no helper: %s missing", FIRST);
      notify("Round2 starter: put round2d.elf in /data/payloads/ (FTP), then restart the PS4.");
      while (!exists(CUR)) { sleep(30); if (!copy_file(FIRST, CUR)) break; }
    }
    blog("first start: copied %s", FIRST);
  }
  if (exists(TRIALF)) { get(TRIALF, trial_v, sizeof trial_v); trial_t = mono(); } /* restarted mid-trial: the trial goes on */
  double launches[4] = { -1e9, -1e9, -1e9, -1e9 };
  int li = 0, held = 0;
  if (!helper_running()) { start_and_wait("PS4 started"); launches[li++ % 4] = mono(); }
  for (;;) {
    sleep(5);
    int run = helper_running();
    if (exists(SWITCHF)) {
      if (!run) { swap_in(); launches[li++ % 4] = mono(); }
      continue; /* the helper is still finishing (it exits by itself after writing switch) */
    }
    if (trial_v[0]) {
      char h[40];
      get(HELLO, h, sizeof h);
      if (!strcmp(h, trial_v)) {
        blog("%s said hello - kept", trial_v);
        unlink(TRIALF);
        unlink(ROLLED);
        trial_v[0] = 0;
      } else if (mono() - trial_t > TRIAL_S) { rollback("no hello within 3 minutes"); continue; }
    }
    if (!run) {
      if (trial_v[0] && ++crashes >= 2) { rollback("stopped twice"); continue; }
      /* a helper that keeps stopping: at most 4 starts in 10 minutes, then wait */
      if (mono() - launches[li % 4] < 600) {
        if (!held++) { blog("the helper keeps stopping - waiting"); notify("Round2 helper keeps stopping - see the control centre (TVs > PS4s)."); }
        continue;
      }
      held = 0;
      start_and_wait(trial_v[0] ? "stopped during trial" : "it had stopped");
      launches[li++ % 4] = mono();
    }
  }
  return 0;
}
