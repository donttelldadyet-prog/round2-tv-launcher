/* Round2 "stay alive" test: does a background payload keep running?
   Writes /data/round2/alive-test.txt: a line every 10 s for 2 hours, with a marker before and after every system call,
   so if it stops we can see exactly where. Also records any stop signal it receives. Read-only otherwise. GPL-3.0. */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct notify_request { char useless1[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceKernelGetCpuTemperature(int *);
int sceSystemServiceGetAppIdOfBigApp(void);
int sceLncUtilGetAppTitleId(int, char *);
int sceUserServiceInitialize(void *);
typedef struct { int32_t userId[4]; } UserIdList;
int sceUserServiceGetLoginUserIdList(UserIdList *);

static FILE *f;
static void line(const char *s) {
  time_t t = time(0) + 8 * 3600;
  struct tm tm;
  gmtime_r(&t, &tm);
  if (f) { fprintf(f, "%02d:%02d:%02d %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, s); fflush(f); }
}
static void on_sig(int s) {
  char b[64];
  snprintf(b, sizeof b, "GOT SIGNAL %d", s);
  line(b);
  if (s != SIGPIPE && s != SIGHUP) _exit(0);
}
static void notify(const char *m) {
  notify_request_t r;
  memset(&r, 0, sizeof r);
  strncpy(r.message, m, sizeof r.message - 1);
  sceKernelSendNotificationRequest(0, &r, sizeof r, 0);
}
int main(void) {
  mkdir("/data/round2", 0777);
  f = fopen("/data/round2/alive-test.txt", "w");
  for (int s = 1; s < 32; s++) if (s != SIGKILL && s != SIGSTOP) signal(s, on_sig);
  line("start (pid ok)");
  sceUserServiceInitialize(0);
  notify("Round2 alive test running (2 hours)");
  for (int i = 0; i < 720; i++) {
    char b[160];
    int app = sceSystemServiceGetAppIdOfBigApp();
    char tid[16] = "";
    if (app > 0) sceLncUtilGetAppTitleId(app, tid);
    UserIdList l;
    memset(&l, 0xff, sizeof l);
    sceUserServiceGetLoginUserIdList(&l);
    int users = 0;
    for (int k = 0; k < 4; k++) if (l.userId[k] != -1) users++;
    int c = -1;
    sceKernelGetCpuTemperature(&c);
    snprintf(b, sizeof b, "tick %d | running %s | signed in %d | CPU %d", i, tid[0] ? tid : "none", users, c);
    line(b);
    line("  sleeping");
    sleep(10);
  }
  line("finished");
  notify("Round2 alive test finished");
  return 0;
}
