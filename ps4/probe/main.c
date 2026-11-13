/* Round2 PS4 probe v6 – checks what a Round2 PS4 helper can do on a real room PS4. Read-only.
   Writes /data/round2/probe.txt, then keeps running for 30 minutes writing /data/round2/heartbeat.txt
   every 30 s (tests a background helper: what's running, who's signed in, temperature).
   GPL-3.0 (built with ps4-payload-dev/sdk; TLS via mbedTLS, Apache-2.0). */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "r2tls.h"
#include "sqlite3.h"

/* ---- PS4 system functions ---- */
typedef struct notify_request { char useless1[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceKernelGetCpuTemperature(int *);
int sceKernelGetSocSensorTemperature(int, int *);

int sceUserServiceInitialize(void *);
typedef struct { int32_t userId[4]; } UserIdList;
int sceUserServiceGetLoginUserIdList(UserIdList *);
int sceUserServiceGetUserName(int32_t, char *, size_t);

int sceRegMgrGetBin(int, void *, int);
int sceRegMgrGetStr(int, char *, int);

int sceSystemServiceGetAppIdOfBigApp(void);
int sceLncUtilGetAppTitleId(int, char *);

int sceKernelGetOpenPsId(uint8_t *);
int scePadInit(void);
int scePadOpen(int32_t, int32_t, int32_t, void *);
int scePadClose(int32_t);
int scePadGetControllerInformation(int32_t, void *);
int scePadReadState(int32_t, void *);
int scePadIsDS4Connected(void);
int sceNetCtlInit(void);
int sceNetCtlGetInfo(int, void *);

/* ---- output ---- */
static FILE *out;
static char summary[700];

static void say(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (out) { vfprintf(out, fmt, ap); fflush(out); }
  va_end(ap);
}
static void sum(const char *fmt, ...) {
  size_t n = strlen(summary);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(summary + n, sizeof summary - n, fmt, ap);
  va_end(ap);
}
static void notify(const char *msg) {
  notify_request_t req;
  memset(&req, 0, sizeof req);
  strncpy(req.message, msg, sizeof req.message - 1);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}
static void clean(char *s) { for (; *s; s++) if (*s == '\n' || *s == '\r' || *s == '\t') *s = ' '; }
static void sg_time(char *b, size_t n) {
  time_t t = time(0) + 8 * 3600;
  struct tm tm;
  gmtime_r(&t, &tm);
  snprintf(b, n, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* ---- param.sfo ---- */
static int sfo_get(const uint8_t *b, size_t n, const char *key, char *val, size_t vlen) {
  if (n < 20 || memcmp(b, "\0PSF", 4)) return -1;
  uint32_t kt = *(uint32_t *)(b + 8), dt = *(uint32_t *)(b + 12), cnt = *(uint32_t *)(b + 16);
  for (uint32_t i = 0; i < cnt; i++) {
    const uint8_t *e = b + 20 + i * 16;
    if (e + 16 > b + n) return -1;
    uint16_t ko = *(uint16_t *)e, fmt = *(uint16_t *)(e + 2);
    uint32_t len = *(uint32_t *)(e + 4), off = *(uint32_t *)(e + 12);
    if (kt + ko >= n || strcmp((const char *)b + kt + ko, key)) continue;
    if (dt + off + len > n) return -1;
    if (fmt == 0x0404) { snprintf(val, vlen, "%u", *(uint32_t *)(b + dt + off)); return 0; }
    size_t l = len < vlen - 1 ? len : vlen - 1;
    memcpy(val, b + dt + off, l);
    val[l] = 0;
    return 0;
  }
  return -1;
}
static int read_file(const char *path, uint8_t *buf, size_t max) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  int n = (int)fread(buf, 1, max, f);
  fclose(f);
  return n;
}
static uint32_t be32(uint32_t v) { return __builtin_bswap32(v); }
/* param.sfo straight out of the installed game package (/user/app/<ID>/app.pkg) */
static int sfo_from_pkg(const char *pkg, uint8_t *buf, size_t max) {
  FILE *f = fopen(pkg, "rb");
  if (!f) return -1;
  uint32_t magic = 0, count = 0, table = 0;
  fread(&magic, 4, 1, f);
  if (magic != 0x544E437Fu) { fclose(f); return -2; }
  fseek(f, 0x0C, SEEK_SET); fread(&count, 4, 1, f);
  fseek(f, 0x18, SEEK_SET); fread(&table, 4, 1, f);
  count = be32(count); table = be32(table);
  fseek(f, table, SEEK_SET);
  for (uint32_t i = 0; i < count && i < 4096; i++) {
    uint32_t e[8];
    if (fread(e, 32, 1, f) != 1) break;
    if (e[0] == 1048576) { /* param.sfo */
      uint32_t off = be32(e[4]), size = be32(e[5]);
      if (size > max) size = max;
      fseek(f, off, SEEK_SET);
      int n = (int)fread(buf, 1, size, f);
      fclose(f);
      return n;
    }
  }
  fclose(f);
  return -3;
}

/* ---- system ---- */
static void system_info(void) {
  say("\n== System\n");
  int sdk = 0;
  size_t l = sizeof sdk;
  if (!sysctlbyname("kern.sdk_version", &sdk, &l, 0, 0)) say("firmware sdk 0x%08x\n", sdk);
  char t[16];
  sg_time(t, sizeof t);
  say("PS4 clock: %s Singapore time\n", t);
  struct timeval bt;
  l = sizeof bt;
  if (!sysctlbyname("kern.boottime", &bt, &l, 0, 0)) {
    long up = (long)(time(0) - bt.tv_sec);
    say("up for %ldh %ldm (since last restart)\n", up / 3600, (up % 3600) / 60);
  }
  int c = -1, s = -1;
  sceKernelGetCpuTemperature(&c);
  sceKernelGetSocSensorTemperature(0, &s);
  say("temperature: CPU %d C, SoC %d C\n", c, s);
  sum("Temp: %d C\n", c);
  struct statfs fs;
  if (!statfs("/user", &fs)) {
    double fr = (double)fs.f_bavail * fs.f_bsize / 1e9, tot = (double)fs.f_blocks * fs.f_bsize / 1e9;
    say("disk: %.1f GB free of %.1f GB\n", fr, tot);
    sum("Disk free: %.0f GB\n", fr);
  }
}

/* ---- network ---- */
static void wifi(void) {
  say("\n== Network\n");
  sceNetCtlInit();
  static uint8_t info[1024];
  memset(info, 0, sizeof info);
  if (!sceNetCtlGetInfo(1, info)) say("connection type code: %u (1 = Wi-Fi, 0 = cable)\n", *(uint32_t *)info);
  memset(info, 0, sizeof info);
  if (!sceNetCtlGetInfo(6, info)) say("Wi-Fi name: %.32s\n", (char *)info);
  memset(info, 0, sizeof info);
  int pct = -1, dbm = 0;
  if (!sceNetCtlGetInfo(9, info)) pct = info[0];
  memset(info, 0, sizeof info);
  if (!sceNetCtlGetInfo(8, info)) dbm = info[0];
  say("signal: %d%% (raw dBm byte %d)\n", pct, dbm);
  memset(info, 0, sizeof info);
  if (!sceNetCtlGetInfo(10, info)) say("channel: %d\n", info[0]);
  memset(info, 0, sizeof info);
  if (!sceNetCtlGetInfo(14, info)) say("IP address: %.16s\n", (char *)info);
  if (pct >= 0) sum("Wi-Fi: %d%%\n", pct);
}

/* ---- profiles ---- */
static void profiles(void) {
  say("\n== Profiles\n");
  sceUserServiceInitialize(0);
  UserIdList l;
  memset(&l, 0xff, sizeof l);
  sceUserServiceGetLoginUserIdList(&l);
  char name[64];
  int n = 0;
  for (int i = 0; i < 4; i++) {
    if (l.userId[i] == -1) continue;
    name[0] = 0;
    sceUserServiceGetUserName(l.userId[i], name, sizeof name);
    say("signed in now: %s (user %08x)\n", name, l.userId[i]);
    if (!n++) sum("Profile: %s\n", name);
  }
  say("signed in: %d\n", n);
  say("all profiles on this PS4 (registry slot: name, account ID):\n");
  for (int i = 1; i <= 16; i++) {
    char un[0x20] = { 0 };
    uint64_t acc = 0;
    sceRegMgrGetStr((i - 1) * 65536 + 125829632, un, 17);
    if (!un[0]) continue;
    sceRegMgrGetBin((i - 1) * 65536 + 125830400, &acc, 8);
    say("  %2d: %-16s account %016llx\n", i, un, (unsigned long long)acc);
  }
}

/* ---- games ---- */
static int is_title_id(const char *s) {
  if (strlen(s) != 9) return 0;
  for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return 0;
  for (int i = 4; i < 9; i++) if (s[i] < '0' || s[i] > '9') return 0;
  return 1;
}
static void games(void) {
  say("\n== Installed (title id | category | version | name | icon)\n");
  static uint8_t buf[65536];
  DIR *d = opendir("/user/app");
  if (!d) { say("can't open /user/app\n"); return; }
  struct dirent *e;
  int n = 0, named = 0, frompkg = 0;
  while ((e = readdir(d))) {
    if (!is_title_id(e->d_name)) continue;
    char p[160], title[256] = "?", cat[16] = "?", ver[16] = "?";
    snprintf(p, sizeof p, "/user/appmeta/%s/param.sfo", e->d_name);
    errno = 0;
    int len = read_file(p, buf, sizeof buf);
    int e1 = errno;
    if (n == 0) {
      say("  (first game: appmeta param.sfo -> %d, errno %d; files:", len, e1);
      char dp[128];
      snprintf(dp, sizeof dp, "/user/appmeta/%s", e->d_name);
      DIR *md = opendir(dp);
      if (md) { struct dirent *f; while ((f = readdir(md))) say(" %s", f->d_name); closedir(md); }
      say(")\n");
    }
    if (len <= 0 || memcmp(buf, "\0PSF", 4)) {
      snprintf(p, sizeof p, "/user/app/%s/app.pkg", e->d_name);
      len = sfo_from_pkg(p, buf, sizeof buf);
      if (n == 0) say("  (first game: app.pkg -> %d)\n", len);
      if (len > 0) frompkg++;
    }
    if (len > 0) {
      sfo_get(buf, len, "TITLE", title, sizeof title);
      sfo_get(buf, len, "CATEGORY", cat, sizeof cat);
      if (sfo_get(buf, len, "APP_VER", ver, sizeof ver)) sfo_get(buf, len, "VERSION", ver, sizeof ver);
    }
    clean(title);
    if (strcmp(title, "?")) named++;
    struct stat st;
    snprintf(p, sizeof p, "/user/appmeta/%s/icon0.png", e->d_name);
    int icon = stat(p, &st) == 0 ? (int)(st.st_size / 1024) : -1;
    say("%s | %s | %s | %s | %s\n", e->d_name, cat, ver, title, icon >= 0 ? "icon ok" : "no icon");
    if (n == 0 && icon >= 0) say("  (icon size %d KB)\n", icon);
    n++;
  }
  closedir(d);
  say("total %d, named %d (from game package %d)\n", n, named, frompkg);
  sum("Games: %d, named %d\n", n, named);
}

/* ---- saves ---- */
static uint64_t dir_size(const char *path, int depth) {
  uint64_t total = 0;
  DIR *d = opendir(path);
  if (!d) return 0;
  struct dirent *e;
  char p[512];
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    snprintf(p, sizeof p, "%s/%s", path, e->d_name);
    struct stat st;
    if (lstat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) { if (depth < 6) total += dir_size(p, depth + 1); }
    else total += st.st_size;
  }
  closedir(d);
  return total;
}
static void saves(void) {
  say("\n== Saves per profile (games, total size, biggest)\n");
  DIR *h = opendir("/user/home");
  if (!h) { say("can't open /user/home\n"); return; }
  struct dirent *u;
  uint64_t all = 0;
  while ((u = readdir(h))) {
    if (u->d_name[0] == '.') continue;
    char p[200], name[64] = "";
    sceUserServiceGetUserName((int32_t)strtoul(u->d_name, 0, 16), name, sizeof name);
    snprintf(p, sizeof p, "/user/home/%s/savedata", u->d_name);
    DIR *s = opendir(p);
    int n = 0;
    uint64_t tot = 0, big = 0;
    char bigid[32] = "";
    if (s) {
      struct dirent *t;
      while ((t = readdir(s))) {
        if (t->d_name[0] == '.' || strncmp(t->d_name, "CUSA", 4)) continue;
        char q[300];
        snprintf(q, sizeof q, "%s/%s", p, t->d_name);
        uint64_t sz = dir_size(q, 0);
        tot += sz;
        n++;
        if (sz > big) { big = sz; snprintf(bigid, sizeof bigid, "%s", t->d_name); }
      }
      closedir(s);
    }
    all += tot;
    say("user %s (%s): %d games, %.1f MB, biggest %s %.1f MB\n", u->d_name, name[0] ? name : "?", n, tot / 1e6, bigid,
        big / 1e6);
  }
  closedir(h);
  say("all profiles together: %.1f MB\n", all / 1e6);
  sum("Saves: %.0f MB\n", all / 1e6);
}

/* ---- running game ---- */
static void running(char *tid, size_t n) {
  int app = sceSystemServiceGetAppIdOfBigApp();
  tid[0] = 0;
  if (app > 0) {
    char t[16] = { 0 };
    if (!sceLncUtilGetAppTitleId(app, t)) snprintf(tid, n, "%s", t);
    else snprintf(tid, n, "app %x", app);
  } else snprintf(tid, n, "none");
}

/* ---- DNS / web ---- */
static const char *lookup(const char *host, char *ip, size_t iplen) {
  struct addrinfo hints, *res = 0;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  int r = getaddrinfo(host, 0, &hints, &res);
  if (r || !res) { snprintf(ip, iplen, "no answer (%d)", r); return ip; }
  inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ip, iplen);
  freeaddrinfo(res);
  return ip;
}
static void dns(void) {
  say("\n== DNS (via nanoDNS)\n");
  const char *hosts[] = { "ps4-system.sec.np.dl.playstation.net", "fus01.ps4.update.playstation.net",
                          "www.round2.sg", "round2-book.donttelldadyet.workers.dev", "time1.google.com" };
  char ip[64];
  for (int i = 0; i < 5; i++) say("%s -> %s\n", hosts[i], lookup(hosts[i], ip, sizeof ip));
  lookup(hosts[0], ip, sizeof ip);
  sum("Sony: %s\n", !strcmp(ip, "0.0.0.0") || !strncmp(ip, "no answer", 9) ? "BLOCKED" : "NOT BLOCKED!");
}
static void https(void) {
  say("\n== HTTPS (Round2's own TLS, certificates checked)\n");
  static char body[2048];
  char err[300];
  struct { const char *host, *path; } t[] = { { "www.round2.sg", "/" },
                                             { "round2-book.donttelldadyet.workers.dev", "/time" } };
  for (int i = 0; i < 2; i++) {
    struct timeval a, b;
    gettimeofday(&a, 0);
    int st = r2tls_request(t[i].host, "GET", t[i].path, 0, 0, 0, body, sizeof body, err, sizeof err);
    gettimeofday(&b, 0);
    long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_usec - a.tv_usec) / 1000;
    say("https://%s%s -> %d in %ld ms %s\n", t[i].host, t[i].path, st, ms, err);
    if (i == 1 && st > 0) { clean(body); say("engine says: %.300s\n", body); }
    sum("%s: %d\n", i ? "Engine" : "Round2.sg", st);
  }
}


/* ---- v4 extras ---- */
static int count_dir(const char *path, uint64_t *bytes, time_t *newest, int depth) {
  DIR *d = opendir(path);
  if (!d) return -1;
  int n = 0;
  struct dirent *e;
  char p[512];
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    snprintf(p, sizeof p, "%s/%s", path, e->d_name);
    struct stat st;
    if (lstat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) { if (depth < 6) { int k = count_dir(p, bytes, newest, depth + 1); if (k > 0) n += k; } }
    else { n++; if (bytes) *bytes += st.st_size; if (newest && st.st_mtime > *newest) *newest = st.st_mtime; }
  }
  closedir(d);
  return n;
}
static void when(time_t t, char *b, size_t n) {
  if (!t) { snprintf(b, n, "-"); return; }
  t += 8 * 3600;
  struct tm tm;
  gmtime_r(&t, &tm);
  snprintf(b, n, "%d/%d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
}
static void print_file(const char *path, int maxlines) {
  FILE *f = fopen(path, "r");
  if (!f) { say("  (no %s)\n", path); return; }
  char line[300];
  int n = 0;
  say("  %s:\n", path);
  while (fgets(line, sizeof line, f) && n++ < maxlines) say("    %s", line);
  if (n && line[strlen(line) - 1] != '\n') say("\n");
  fclose(f);
}
static void extras(void) {
  say("\n== Waiting system update? (any file here = an update was downloaded)\n");
  const char *upd[] = { "/update", "/system_data/update", "/user/update", "/system_data/priv/update" };
  int found = 0;
  for (int i = 0; i < 4; i++) {
    DIR *d = opendir(upd[i]);
    if (!d) continue;
    struct dirent *e;
    while ((e = readdir(d))) {
      if (e->d_name[0] == '.') continue;
      char p[300];
      struct stat st;
      snprintf(p, sizeof p, "%s/%s", upd[i], e->d_name);
      if (!stat(p, &st)) { say("  %s  %.1f MB\n", p, st.st_size / 1e6); if (st.st_size > 1000000) found++; }
    }
    closedir(d);
  }
  say("  big update files found: %d\n", found);
  sum("Update waiting: %s\n", found ? "YES - check!" : "no");


  say("\n== Game updates (downloads waiting + installed patches)\n");
  {
    int fw = 0;
    size_t l = sizeof fw;
    sysctlbyname("kern.sdk_version", &fw, &l, 0, 0);
    static uint8_t sb[65536];
    DIR *dd = opendir("/user/download");
    double dl = 0;
    int dn = 0;
    if (dd) {
      struct dirent *e;
      while ((e = readdir(dd))) {
        if (e->d_name[0] == '.') continue;
        char p[300];
        uint64_t by = 0;
        time_t nw2 = 0;
        char w2[32];
        snprintf(p, sizeof p, "/user/download/%s", e->d_name);
        int k = count_dir(p, &by, &nw2, 0);
        when(nw2, w2, sizeof w2);
        say("  download folder %s: %d files, %.1f MB, last %s\n", e->d_name, k, by / 1e6, w2);
        dl += by / 1e6;
        dn++;
      }
      closedir(dd);
    } else say("  (no /user/download)\n");
    say("  downloads waiting: %d, %.0f MB\n", dn, dl);
    DIR *pd = opendir("/user/patch");
    double pt = 0, bad = 0;
    int pn = 0, pbad = 0;
    if (pd) {
      struct dirent *e;
      while ((e = readdir(pd))) {
        if (!is_title_id(e->d_name)) continue;
        char p[300], ver[16] = "?", sys[16] = "0";
        uint64_t by = 0;
        snprintf(p, sizeof p, "/user/patch/%s", e->d_name);
        count_dir(p, &by, 0, 0);
        snprintf(p, sizeof p, "/user/patch/%s/patch.pkg", e->d_name);
        int len = sfo_from_pkg(p, sb, sizeof sb);
        if (len > 0) {
          sfo_get(sb, len, "APP_VER", ver, sizeof ver);
          sfo_get(sb, len, "SYSTEM_VER", sys, sizeof sys);
        }
        unsigned need = (unsigned)strtoul(sys, 0, 10);
        int toonew = need && fw && need > (unsigned)fw;
        say("  patch %s: v%s, needs firmware %x.%02x, %.1f MB%s\n", e->d_name, ver, need >> 24, (need >> 16) & 0xff,
            by / 1e6, toonew ? "  <-- TOO NEW for this PS4" : "");
        pt += by / 1e6;
        pn++;
        if (toonew) { pbad++; bad += by / 1e6; }
      }
      closedir(pd);
    } else say("  (no /user/patch)\n");
    say("  installed patches: %d, %.0f MB; too new for this firmware: %d, %.0f MB\n", pn, pt, pbad, bad);
    sum("Game updates: %d waiting (%.0f MB), %d too new\n", dn, dl, pbad);
  }

  say("\n== GoldHEN settings\n");
  print_file("/data/GoldHEN/config.ini", 60);
  print_file("/data/GoldHEN/payloads.ini", 20);

  say("\n== nanoDNS log (what was blocked since nanoDNS started)\n");
  FILE *f = fopen("/data/nanodns/nanodns.log", "r");
  if (f) {
    char line[400], names[12][100];
    int blocked = 0, allowed = 0, uniq = 0;
    while (fgets(line, sizeof line, f)) {
      char *m = strstr(line, "override matched * -> 0.0.0.0 for ");
      if (m) {
        blocked++;
        m += strlen("override matched * -> 0.0.0.0 for ");
        m[strcspn(m, "\r\n")] = 0;
        int seen = 0;
        for (int i = 0; i < uniq; i++) if (!strcmp(names[i], m)) seen = 1;
        if (!seen && uniq < 12) snprintf(names[uniq++], 100, "%s", m);
      } else if (strstr(line, "response via=upstream")) allowed++;
    }
    fclose(f);
    say("  blocked %d lookups, allowed %d\n", blocked, allowed);
    for (int i = 0; i < uniq; i++) say("  blocked: %s\n", names[i]);
  } else say("  (no log – nanoDNS not running?)\n");

  say("\n== Captures (screenshots / video clips guests leave behind)\n");
  uint64_t b = 0;
  time_t nw = 0;
  char w[32];
  int n = count_dir("/user/av_contents", &b, &nw, 0);
  when(nw, w, sizeof w);
  say("  %d files, %.1f MB, newest %s\n", n, b / 1e6, w);

  say("\n== Disc / USB\n");
  DIR *d = opendir("/mnt/disc");
  int disc = 0;
  if (d) { struct dirent *e; while ((e = readdir(d))) if (e->d_name[0] != '.') disc++; closedir(d); }
  say("  disc drive: %s\n", disc ? "a disc is in" : "empty");
  for (int i = 0; i < 8; i++) {
    char p[32];
    snprintf(p, sizeof p, "/mnt/usb%d", i);
    d = opendir(p);
    if (!d) continue;
    int k = 0;
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') k++;
    closedir(d);
    if (k) say("  USB plugged in: %s (%d items)\n", p, k);
  }

  say("\n== Crashes\n");
  nw = 0;
  n = count_dir("/data/sce_coredumps", 0, &nw, 0);
  when(nw, w, sizeof w);
  say("  crash dumps: %d, newest %s\n", n < 0 ? 0 : n, w);
}


/* ---- v6 ---- */
static void hexdump(const uint8_t *b, int n, char *out, size_t on) {
  size_t k = 0;
  out[0] = 0;
  for (int i = 0; i < n && k + 3 < on; i++) k += snprintf(out + k, on - k, "%02x", b[i]);
}
static void identity(void) {
  say("\n== Console identity\n");
  uint8_t id[16] = { 0 };
  char hx[64];
  if (!sceKernelGetOpenPsId(id)) { hexdump(id, 16, hx, sizeof hx); say("console ID: %s\n", hx); }
  else say("console ID: not readable\n");
  static uint8_t info[1024];
  memset(info, 0, sizeof info);
  if (!sceNetCtlGetInfo(2, info))
    say("network address (MAC): %02x:%02x:%02x:%02x:%02x:%02x\n", info[0], info[1], info[2], info[3], info[4], info[5]);
}
static void ext_storage(void) {
  say("\n== Extended storage (USB drive used for games)\n");
  int any = 0;
  for (int i = 0; i < 2; i++) {
    char p[64];
    snprintf(p, sizeof p, "/mnt/ext%d", i);
    struct statfs fs;
    if (statfs(p, &fs)) continue;
    any = 1;
    double fr = (double)fs.f_bavail * fs.f_bsize / 1e9, tot = (double)fs.f_blocks * fs.f_bsize / 1e9;
    say("%s: %.1f GB free of %.1f GB\n", p, fr, tot);
    snprintf(p, sizeof p, "/mnt/ext%d/user/app", i);
    DIR *d = opendir(p);
    int n = 0;
    if (d) {
      struct dirent *e;
      say("  games on it:");
      while ((e = readdir(d))) if (is_title_id(e->d_name)) { say(" %s", e->d_name); n++; }
      closedir(d);
      say("  (%d)\n", n);
    }
    sum("Ext storage: %.0f GB free, %d games\n", fr, n);
  }
  if (!any) say("none\n");
}
static void controllers(void) {
  say("\n== Controllers\n");
  scePadInit();
  say("DS4 controllers connected (system count): %d\n", scePadIsDS4Connected());
  UserIdList l;
  memset(&l, 0xff, sizeof l);
  sceUserServiceGetLoginUserIdList(&l);
  for (int i = 0; i < 4; i++) {
    if (l.userId[i] == -1) continue;
    char name[64] = "";
    sceUserServiceGetUserName(l.userId[i], name, sizeof name);
    int h = scePadOpen(l.userId[i], 0, 0, 0);
    if (h < 0) { say("%s: can't open (0x%08x – a game may be using it)\n", name, h); continue; }
    static uint8_t ci[256], st[1024];
    memset(ci, 0, sizeof ci);
    memset(st, 0, sizeof st);
    int r1 = scePadGetControllerInformation(h, ci);
    int r2 = scePadReadState(h, st);
    char a[160], b[600];
    hexdump(ci, 32, a, sizeof a);
    hexdump(st, 0x80, b, sizeof b);
    say("%s: handle %d, info(%x) %s\n  state(%x) %s\n", name, h, r1, a, r2, b);
    scePadClose(h);
  }
}
static int print_row(void *u, int n, char **v, char **c) {
  (void)u;
  say("  ");
  for (int i = 0; i < n; i++) say("%s=%s%s", c[i], v[i] ? v[i] : "", i < n - 1 ? " | " : "\n");
  return 0;
}
static void play_history(void) {
  say("\n== Play history (the PS4's own app list)\n");
  /* work on a copy so the system's database is never touched */
  static uint8_t buf[1 << 16];
  FILE *in = fopen("/system_data/priv/mms/app.db", "rb");
  if (!in) { say("can't open app.db (errno %d)\n", errno); return; }
  FILE *o = fopen("/data/round2/app-copy.db", "wb");
  size_t n, tot = 0;
  while ((n = fread(buf, 1, sizeof buf, in)) > 0) { fwrite(buf, 1, n, o); tot += n; }
  fclose(in);
  fclose(o);
  say("copied app.db (%.1f MB)\n", tot / 1e6);
  sqlite3 *db;
  if (sqlite3_open_v2("/data/round2/app-copy.db", &db, SQLITE_OPEN_READONLY, 0)) { say("can't read copy\n"); return; }
  char *err = 0;
  say("tables:\n");
  sqlite3_exec(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name", print_row, 0, &err);
  sqlite3_stmt *q;
  if (!sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_appbrowse%'", -1, &q, 0)) {
    while (sqlite3_step(q) == SQLITE_ROW) {
      const char *t = (const char *)sqlite3_column_text(q, 0);
      char sql[600];
      say("\n%s (columns):\n", t);
      snprintf(sql, sizeof sql, "SELECT group_concat(name, ', ') AS cols FROM pragma_table_info('%s')", t);
      sqlite3_exec(db, sql, print_row, 0, &err);
      say("%s (games, newest first):\n", t);
      snprintf(sql, sizeof sql,
               "SELECT titleId, titleName, lastAccessTime, installDate, contentSize FROM \"%s\" "
               "WHERE titleId LIKE 'CUSA%%' ORDER BY lastAccessTime DESC LIMIT 80", t);
      if (sqlite3_exec(db, sql, print_row, 0, &err)) { say("  (%s)\n", err ? err : "?"); sqlite3_free(err); err = 0; }
    }
    sqlite3_finalize(q);
  }
  sqlite3_close(db);
  unlink("/data/round2/app-copy.db");
}
static void profile_activity(void) {
  say("\n== Profile activity (newest save change per profile)\n");
  DIR *h = opendir("/user/home");
  if (!h) return;
  struct dirent *u;
  while ((u = readdir(h))) {
    if (u->d_name[0] == '.') continue;
    char p[200], name[64] = "", w[32];
    sceUserServiceGetUserName((int32_t)strtoul(u->d_name, 0, 16), name, sizeof name);
    snprintf(p, sizeof p, "/user/home/%s/savedata", u->d_name);
    time_t nw = 0;
    count_dir(p, 0, &nw, 0);
    when(nw, w, sizeof w);
    say("%s (%s): last save %s\n", name[0] ? name : "?", u->d_name, w);
  }
  closedir(h);
}
static void speed(void) {
  say("\n== Connection speed (download of the round2.sg page)\n");
  static char body[512];
  char err[200];
  struct timeval a, b;
  gettimeofday(&a, 0);
  int st = r2tls_request("www.round2.sg", "GET", "/", 0, 0, 0, body, sizeof body, err, sizeof err);
  gettimeofday(&b, 0);
  double s2 = (b.tv_sec - a.tv_sec) + (b.tv_usec - a.tv_usec) / 1e6;
  double mbit = s2 > 0 ? r2tls_last_bytes * 8 / 1e6 / s2 : 0;
  say("status %d, %.0f KB in %.2f s = about %.1f Mbit/s %s\n", st, r2tls_last_bytes / 1e3, s2, mbit, err);
  sum("Speed: ~%.1f Mbit/s\n", mbit);
}
static void jailbreak_time(void) {
  say("\n== Restart and jailbreak times\n");
  struct timeval bt;
  size_t l = sizeof bt;
  char w[32];
  if (!sysctlbyname("kern.boottime", &bt, &l, 0, 0)) { when(bt.tv_sec, w, sizeof w); say("last restart: %s\n", w); }
  struct stat st;
  if (!stat("/data/nanodns/nanodns.log", &st)) {
    when(st.st_birthtime ? st.st_birthtime : st.st_ctime, w, sizeof w);
    say("nanoDNS started (≈ jailbreak): %s\n", w);
    long gap = (long)((st.st_birthtime ? st.st_birthtime : st.st_ctime) - bt.tv_sec);
    say("jailbreak came %ld min after the restart\n", gap / 60);
  } else say("nanoDNS log missing – nanoDNS not running since the last restart?\n");
}

/* ---- background test ---- */
static void heartbeat(void) {
  FILE *h = fopen("/data/round2/heartbeat.txt", "w");
  if (!h) return;
  fprintf(h, "Round2 background test: a line every 30 s for 30 minutes (time | running | signed in | controllers | CPU C)\n");
  fflush(h);
  char seen[20][32];
  int mins[20] = { 0 }, ns = 0;
  for (int i = 0; i < 60; i++) {
    char t[16], run[32];
    sg_time(t, sizeof t);
    running(run, sizeof run);
    UserIdList l;
    memset(&l, 0xff, sizeof l);
    sceUserServiceGetLoginUserIdList(&l);
    int users = 0;
    for (int k = 0; k < 4; k++) if (l.userId[k] != -1) users++;
    int c = -1;
    sceKernelGetCpuTemperature(&c);
    fprintf(h, "%s | %s | %d | %d | %d\n", t, run, users, scePadIsDS4Connected(), c);
    int k;
    for (k = 0; k < ns && strcmp(seen[k], run); k++) ;
    if (k == ns && ns < 20) snprintf(seen[ns++], 32, "%s", run);
    if (k < 20) mins[k]++;
    fflush(h);
    sleep(30);
  }
  fprintf(h, "finished. time per app (minutes):\n");
  for (int k = 0; k < ns; k++) fprintf(h, "  %s: %.1f\n", seen[k], mins[k] / 2.0);
  fclose(h);
}

int main(void) {
  mkdir("/data/round2", 0777);
  out = fopen("/data/round2/probe.txt", "w");
  say("Round2 PS4 probe v6\n");
  notify("Round2 probe: checking… (about 1 minute)");
  system_info();
  identity();
  jailbreak_time();
  wifi();
  profiles();
  {
    char run[32];
    running(run, sizeof run);
    say("\n== Running now: %s\n", run);
  }
  games();
  saves();
  profile_activity();
  ext_storage();
  controllers();
  dns();
  https();
  extras();
  speed();
  play_history();
  say("\nDone. Background test running for 30 minutes -> /data/round2/heartbeat.txt\n");
  if (out) fclose(out);
  sum("Background test: 30 min");
  notify(summary);
  heartbeat();
  notify("Round2 probe: background test finished");
  return 0;
}
