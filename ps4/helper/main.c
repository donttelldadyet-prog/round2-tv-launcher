/* Round2 PS4 helper (round2d) v1 – check-ins.
   Runs in the background after the jailbreak (GoldHEN AutoRun). Every minute it notes which game is running (play time);
   every 15 minutes it reports to the Round2 booking engine: console ID, firmware, temperature, disk, Wi-Fi, who's signed in,
   Sony blocked?, installed games (only when the list changed), stuck downloads (never PS Vue's), captures, crashes,
   minutes played per game. (Controllers are not touched – see add_users.) It changes nothing on the PS4.
   Key: /data/round2/key.txt (one line, the engine's PS4_KEY). Status for staff: /data/round2/helper.txt.
   GPL-3.0. Built with ps4-payload-dev/sdk; TLS via mbedTLS (Apache-2.0). */

#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include "mbedtls/sha256.h"

#include "r2tls.h"
#include <ps4/kernel.h>
#include <sys/ioctl.h>

#define R2_VERSION "1.1.7"
#define ENGINE_HOST "round2-book.donttelldadyet.workers.dev"
#define VUE_ID "CUSA00960" /* PS Vue + its half-downloaded update keep the Vue jailbreak working – never touch, never count */
#define REPORT_EVERY 15     /* minutes */
/* 1.0.4: run by the starter (round2-boot.elf) – see boot/main.c */
#define APP "/data/round2/app"
#define LOCKF "/data/round2/helper.lock"

typedef struct notify_request { char useless1[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceKernelGetCpuTemperature(int *);
int sceKernelGetSocSensorTemperature(int, int *);
int sceKernelGetOpenPsId(uint8_t *);
int sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, int *);
int sceKernelDlsym(int, const char *, void **);
int sceUserServiceInitialize(void *);
typedef struct { int32_t userId[4]; } UserIdList;
int sceUserServiceGetLoginUserIdList(UserIdList *);
int sceUserServiceGetUserName(int32_t, char *, size_t);
/* 1.0.9 profile probe (read-only) */
typedef struct { int32_t userId[16]; int32_t spare[48]; } RegUserIdList;   /* 16 slots + room to spare */
int sceUserServiceGetRegisteredUserIdList(RegUserIdList *);
int sceUserServiceGetInitialUser(int32_t *);
int sceRegMgrGetInt(long, int *);
int sceRegMgrGetStr(long, char *, int);
int sceRegMgrGetBin(long, void *, int);
int sceRegMgrSetInt(long, int);
int sceRegMgrSetStr(long, const char *, int);
int sceRegMgrSetBin(long, const void *, int);
int sceUserServiceSetUserName(int32_t, const char *);
int sceUserServiceLogin(int32_t);
int sceUserServiceLogout(int32_t);
int sceSystemServiceGetAppIdOfBigApp(void);
int sceLncUtilGetAppTitleId(int, char *);
int sceNetCtlInit(void);
int sceNetCtlGetInfo(int, void *);

/* ---------- small helpers ---------- */
static void notify(const char *msg) {
  notify_request_t req;
  memset(&req, 0, sizeof req);
  strncpy(req.message, msg, sizeof req.message - 1);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

/* growing text buffer for the JSON report */
static char *J;
static size_t Jn, Jcap;
static void jadd(const char *fmt, ...) {
  va_list ap;
  for (;;) {
    va_start(ap, fmt);
    int n = vsnprintf(J + Jn, Jcap - Jn, fmt, ap);
    va_end(ap);
    if (n >= 0 && Jn + n < Jcap) { Jn += n; return; }
    Jcap = Jcap ? Jcap * 2 : 65536;
    J = realloc(J, Jcap);
  }
}
static void jstr(const char *s) { /* JSON string with escaping; UTF-8 passes through */
  jadd("\"");
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') jadd("\\%c", c);
    else if (c < 0x20) jadd(" ");
    else jadd("%c", c);
  }
  jadd("\"");
}

static int is_title_id(const char *s) {
  if (strlen(s) != 9) return 0;
  for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return 0;
  for (int i = 4; i < 9; i++) if (s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

/* ---------- param.sfo (from the game package; /user/appmeta has none on these PS4s) ---------- */
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
static int sfo_from_pkg(const char *pkg, uint8_t *buf, size_t max) {
  FILE *f = fopen(pkg, "rb");
  if (!f) return -1;
  uint32_t magic = 0, count = 0, table = 0;
  fread(&magic, 4, 1, f);
  if (magic != 0x544E437Fu) { fclose(f); return -2; }
  fseek(f, 0x0C, SEEK_SET); fread(&count, 4, 1, f);
  fseek(f, 0x18, SEEK_SET); fread(&table, 4, 1, f);
  count = __builtin_bswap32(count); table = __builtin_bswap32(table);
  fseek(f, table, SEEK_SET);
  for (uint32_t i = 0; i < count && i < 4096; i++) {
    uint32_t e[8];
    if (fread(e, 32, 1, f) != 1) break;
    if (e[0] == 1048576) {
      uint32_t off = __builtin_bswap32(e[4]), size = __builtin_bswap32(e[5]);
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

static int count_dir(const char *path, uint64_t *bytes, int depth) {
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
    if (S_ISDIR(st.st_mode)) { if (depth < 6) { int k = count_dir(p, bytes, depth + 1); if (k > 0) n += k; } }
    else { n++; if (bytes) *bytes += st.st_size; }
  }
  closedir(d);
  return n;
}

/* ---------- facts ---------- */
static char console_id[40];
static void get_console_id(void) {
  uint8_t id[16] = { 0 };
  console_id[0] = 0;
  if (sceKernelGetOpenPsId(id)) return;
  for (int i = 0; i < 16; i++) sprintf(console_id + i * 2, "%02x", id[i]);
}

static void running_title(char *tid, size_t n) {
  int app = sceSystemServiceGetAppIdOfBigApp();
  tid[0] = 0;
  if (app > 0) {
    char t[16] = { 0 };
    if (!sceLncUtilGetAppTitleId(app, t)) snprintf(tid, n, "%s", t);
  }
}

static int sony_blocked(void) { /* 1 = blocked (nanoDNS answers 0.0.0.0 or nothing answers), 0 = NOT blocked */
  struct addrinfo hints, *res = 0;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo("ps4-system.sec.np.dl.playstation.net", 0, &hints, &res) || !res) return 1;
  uint32_t a = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(res);
  return a == 0;
}

/* games: JSON array + a simple hash so it's only sent when it changed */
static uint64_t games_json(char **out) {
  static uint8_t buf[65536];
  char *save = J; size_t sn = Jn, sc = Jcap;
  J = 0; Jn = 0; Jcap = 0;
  jadd("[");
  int first = 1;
  DIR *d = opendir("/user/app");
  if (d) {
    struct dirent *e;
    while ((e = readdir(d))) {
      if (!is_title_id(e->d_name)) continue;
      char p[160], title[256] = "", cat[16] = "", ver[16] = "";
      snprintf(p, sizeof p, "/user/app/%s/app.pkg", e->d_name);
      int len = sfo_from_pkg(p, buf, sizeof buf);
      if (len > 0) {
        sfo_get(buf, len, "TITLE", title, sizeof title);
        sfo_get(buf, len, "CATEGORY", cat, sizeof cat);
        sfo_get(buf, len, "APP_VER", ver, sizeof ver);
      }
      char pver[16] = "";
      snprintf(p, sizeof p, "/user/patch/%s/patch.pkg", e->d_name);
      len = sfo_from_pkg(p, buf, sizeof buf);
      if (len > 0) sfo_get(buf, len, "APP_VER", pver, sizeof pver);
      jadd("%s{\"id\":\"%s\",\"t\":", first ? "" : ",", e->d_name);
      jstr(title);
      jadd(",\"cat\":\"%s\",\"v\":\"%s\",\"pv\":\"%s\"}", cat, ver, pver);
      first = 0;
    }
    closedir(d);
  }
  /* games on extended storage (Max) */
  d = opendir("/mnt/ext0/user/app");
  if (d) {
    struct dirent *e;
    while ((e = readdir(d))) {
      if (!is_title_id(e->d_name)) continue;
      char p[160], title[256] = "", cat[16] = "", ver[16] = "";
      snprintf(p, sizeof p, "/mnt/ext0/user/app/%s/app.pkg", e->d_name);
      int len = sfo_from_pkg(p, buf, sizeof buf);
      if (len > 0) {
        sfo_get(buf, len, "TITLE", title, sizeof title);
        sfo_get(buf, len, "CATEGORY", cat, sizeof cat);
        sfo_get(buf, len, "APP_VER", ver, sizeof ver);
      }
      jadd("%s{\"id\":\"%s\",\"t\":", first ? "" : ",", e->d_name);
      jstr(title);
      jadd(",\"cat\":\"%s\",\"v\":\"%s\",\"ext\":1}", cat, ver);
      first = 0;
    }
    closedir(d);
  }
  jadd("]");
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < Jn; i++) h = (h ^ (uint8_t)J[i]) * 1099511628211ull;
  *out = J;
  J = save; Jn = sn; Jcap = sc;
  return h;
}

static void add_wifi(void) {
  static uint8_t info[1024];
  int pct = -1, ch = -1;
  char ssid[40] = "", ip[20] = "";
  memset(info, 0, sizeof info); if (!sceNetCtlGetInfo(6, info)) snprintf(ssid, sizeof ssid, "%.32s", (char *)info);
  memset(info, 0, sizeof info); if (!sceNetCtlGetInfo(9, info)) pct = info[0];
  memset(info, 0, sizeof info); if (!sceNetCtlGetInfo(10, info)) ch = info[0];
  memset(info, 0, sizeof info); if (!sceNetCtlGetInfo(14, info)) snprintf(ip, sizeof ip, "%.16s", (char *)info);
  jadd(",\"wifi\":{\"ssid\":");
  jstr(ssid);
  jadd(",\"pct\":%d,\"ch\":%d,\"ip\":\"%s\"}", pct, ch, ip);
}

static void add_users(void) {
  /* Controllers are NOT opened any more: the probe opened them on Vale (13 Nov) and stopped a minute later, while a
     plain background test that never touched them ran fine through the menu, a game and back. */
  UserIdList l;
  memset(&l, 0xff, sizeof l);
  sceUserServiceGetLoginUserIdList(&l);
  jadd(",\"users\":[");
  int first = 1;
  for (int i = 0; i < 4; i++) {
    if (l.userId[i] == -1) continue;
    char name[64] = "";
    sceUserServiceGetUserName(l.userId[i], name, sizeof name);
    jadd(first ? "" : ",");
    jstr(name);
    first = 0;
  }
  jadd("]");
}

static void add_storage(void) {
  struct statfs fs;
  if (!statfs("/user", &fs)) jadd(",\"disk\":{\"free\":%.1f,\"total\":%.1f}", (double)fs.f_bavail * fs.f_bsize / 1e9, (double)fs.f_blocks * fs.f_bsize / 1e9);
  if (!statfs("/mnt/ext0", &fs) && fs.f_blocks) jadd(",\"ext\":{\"free\":%.1f,\"total\":%.1f}", (double)fs.f_bavail * fs.f_bsize / 1e9, (double)fs.f_blocks * fs.f_bsize / 1e9);
  /* stuck update downloads (never PS Vue's) */
  jadd(",\"downloads\":[");
  DIR *d = opendir("/user/download");
  int first = 1;
  double tot = 0;
  if (d) {
    struct dirent *e;
    while ((e = readdir(d))) {
      if (!is_title_id(e->d_name) || !strcmp(e->d_name, VUE_ID)) continue;
      char p[300];
      uint64_t b = 0;
      snprintf(p, sizeof p, "/user/download/%s", e->d_name);
      count_dir(p, &b, 0);
      jadd("%s{\"id\":\"%s\",\"mb\":%.0f}", first ? "" : ",", e->d_name, b / 1e6);
      tot += b / 1e6;
      first = 0;
    }
    closedir(d);
  }
  jadd("],\"downloadsMB\":%.0f", tot);
  uint64_t b = 0;
  int n = count_dir("/user/av_contents", &b, 0);
  jadd(",\"captures\":{\"n\":%d,\"mb\":%.0f}", n < 0 ? 0 : n, b / 1e6);
  n = count_dir("/data/sce_coredumps", 0, 0);
  jadd(",\"crashes\":%d", n < 0 ? 0 : n);
}

/* ---------- play time ---------- */
static char played_id[32][12];
static int played_min[32], nplayed;
static void note_playing(const char *tid) {
  if (!tid[0] || !strcmp(tid, VUE_ID) || strncmp(tid, "CUSA", 4)) return;
  int k;
  for (k = 0; k < nplayed && strcmp(played_id[k], tid); k++) ;
  if (k == nplayed) { if (nplayed == 32) return; snprintf(played_id[nplayed++], 12, "%s", tid); played_min[k] = 0; }
  played_min[k]++;
}

/* ---------- update guard (1.0.2, owner 13 Nov: refuse a system update, e.g. one coming from a game disc) ----------
   The PS4 puts a system update it is about to install in one of these folders as PS4UPDATE.PUP (downloads: also .net.temp).
   Round2 keeps 0-byte files there as the blocker. Every 2 s: any *PUP* file that has grown is cut back to 0 bytes (the PS4's
   own check then fails and it can't install), and a missing /update/PS4UPDATE.PUP blocker is put back. Game patches and
   PS Vue's download live elsewhere (/user/patch, /user/download) - never looked at here.
   Only works while the PS4 is jailbroken (the helper isn't running before that). */
static const char *UPD_DIRS[] = { "/update", "/system_data/update", "/user/update", "/system_data/priv/update" };
static int upd_blocked, upd_last_mb, disc_pup, disc_warned;
static time_t upd_last;
static int has_pup(const char *n) {
  for (; n[0] && n[1] && n[2]; n++)
    if ((n[0] == 'P' || n[0] == 'p') && (n[1] == 'U' || n[1] == 'u') && (n[2] == 'P' || n[2] == 'p')) return 1;
  return 0;
}
static int update_guard(void) { /* 1 = it stopped something just now */
  int hit = 0;
  struct stat st;
  for (unsigned i = 0; i < sizeof UPD_DIRS / sizeof *UPD_DIRS; i++) {
    DIR *d = opendir(UPD_DIRS[i]);
    if (!d) continue;
    struct dirent *e;
    while ((e = readdir(d))) {
      if (!has_pup(e->d_name)) continue;
      char p[300];
      snprintf(p, sizeof p, "%s/%s", UPD_DIRS[i], e->d_name);
      if (stat(p, &st) || !S_ISREG(st.st_mode) || st.st_size == 0) continue;
      if (truncate(p, 0) == 0) { hit = 1; upd_blocked++; upd_last = time(0); upd_last_mb = (int)(st.st_size >> 20); }
    }
    closedir(d);
  }
  if (stat("/update", &st) == 0 && stat("/update/PS4UPDATE.PUP", &st) != 0) { /* blocker gone -> put it back */
    FILE *f = fopen("/update/PS4UPDATE.PUP", "w");
    if (f) fclose(f);
  }
  /* a disc carrying a system update: warn once per disc (most game discs carry one; the PS4 only offers it if newer) */
  int pup = stat("/mnt/disc/PS4/UPDATE/PS4UPDATE.PUP", &st) == 0 || stat("/mnt/disc/ps4/update/ps4update.pup", &st) == 0;
  if (pup && !disc_warned) { disc_warned = 1; notify("Round2: if this disc asks to update the PS4, choose CANCEL and ask staff"); }
  if (!pup) disc_warned = 0;
  disc_pup = pup;
  return hit;
}

/* ---------- report ---------- */
static uint64_t games_sent_hash;
static int report(const char *key, char *status, size_t slen) {
  char run[32];
  running_title(run, sizeof run);
  Jn = 0;
  jadd("{\"id\":\"%s\",\"v\":\"%s\"", console_id, R2_VERSION);
  int fw = 0;
  size_t l = sizeof fw;
  sysctlbyname("kern.sdk_version", &fw, &l, 0, 0);
  jadd(",\"fw\":\"%x.%02x\"", (fw >> 24) & 0xff, (fw >> 16) & 0xff);
  int c = -1, s = -1;
  sceKernelGetCpuTemperature(&c);
  sceKernelGetSocSensorTemperature(0, &s);
  jadd(",\"temp\":{\"cpu\":%d,\"soc\":%d},\"psClock\":%lld", c, s, (long long)time(0));
  jadd(",\"sony\":%s,\"running\":", sony_blocked() ? "\"blocked\"" : "\"OPEN\"");
  jstr(run);
  add_users();
  add_wifi();
  add_storage();
  jadd(",\"updGuard\":{\"blocked\":%d,\"lastMB\":%d,\"agoS\":%lld,\"discUpd\":%d}", upd_blocked, upd_last_mb,
       upd_last ? (long long)(time(0) - upd_last) : -1LL, disc_pup);
  { char bv[16] = "", rb[200] = "";
    FILE *f = fopen(APP "/boot.ver", "r"); if (f) { if (!fgets(bv, sizeof bv, f)) bv[0] = 0; fclose(f); bv[strcspn(bv, "\r\n")] = 0; }
    f = fopen(APP "/rolled-back.txt", "r"); if (f) { if (!fgets(rb, sizeof rb, f)) rb[0] = 0; fclose(f); rb[strcspn(rb, "\r\n")] = 0; }
    if (bv[0]) { jadd(",\"boot\":"); jstr(bv); }
    if (rb[0]) { jadd(",\"rolled\":"); jstr(rb); } }
  jadd(",\"played\":{");
  for (int k = 0; k < nplayed; k++) jadd("%s\"%s\":%d", k ? "," : "", played_id[k], played_min[k]);
  jadd("}");
  char *g = 0;
  uint64_t gh = games_json(&g);
  if (gh != games_sent_hash) jadd(",\"games\":%s", g);
  free(g);
  jadd("}");

  char hdr[200], body[512], err[200];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  int st = r2tls_request(ENGINE_HOST, "POST", "/ps4/in", hdr, J, Jn, body, sizeof body, err, sizeof err);
  if (st == 200) {
    games_sent_hash = gh;
    nplayed = 0; /* counted by the engine */
  }
  snprintf(status, slen, "%d %s %.200s", st, err, body);
  return st;
}

/* ---------- staff commands (1.0.3, owner 13 Nov: one-tap clean-up from the control centre) ----------
   Every minute the helper asks the engine (/ps4/cmd) whether staff sent something. The engine only hands out a clean-up when
   the room and its twin have no session now or within 15 minutes; the helper also refuses while a game is open.
   Clean-up = stuck update downloads in /user/download (every title EXCEPT PS Vue CUSA00960 – its half download keeps the Vue
   jailbreak working) + guests' screenshots and clips in /user/av_contents (files only, the folders stay).
   Never touched: installed games (/user/app), installed patches (/user/patch), saves, the update blocker files. */
static int is_media(const char *n) { /* captures: pictures and clips only */
  const char *x = strrchr(n, '.');
  if (!x) return 0;
  char e[8] = "";
  for (int i = 0; i < 7 && x[i + 1]; i++) e[i] = (x[i + 1] >= 'A' && x[i + 1] <= 'Z') ? x[i + 1] + 32 : x[i + 1];
  return !strcmp(e, "jpg") || !strcmp(e, "jpeg") || !strcmp(e, "png") || !strcmp(e, "mp4") || !strcmp(e, "webm");
}
static uint64_t rm_files(const char *path, int depth, int *files, int keep_dirs) { /* never follows links; keep_dirs = captures mode (media files only) */
  uint64_t freed = 0;
  DIR *d = opendir(path);
  if (!d) return 0;
  struct dirent *e;
  char p[512];
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    snprintf(p, sizeof p, "%s/%s", path, e->d_name);
    struct stat st;
    if (lstat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) {
      if (depth < 8) { freed += rm_files(p, depth + 1, files, keep_dirs); if (!keep_dirs) rmdir(p); }
    } else if ((!keep_dirs || is_media(e->d_name)) && unlink(p) == 0) { freed += st.st_size; (*files)++; }
  }
  closedir(d);
  return freed;
}
/* 1.0.6: a capture is a small folder (photo|video/<app>/<title>/<xyz>/) holding the picture or clip plus its .dat and .ext
   (what the Capture Gallery lists). 1.0.3-1.0.5 deleted only the picture -> the gallery showed empty tiles. Now every file of
   those kinds goes and the emptied folders too (never photo/ or video/ themselves). content_tmp: media files only. */
static int is_capfile(const char *n) {
  const char *x = strrchr(n, '.');
  if (!x) return 0;
  return is_media(n) || !strcasecmp(x, ".dat") || !strcasecmp(x, ".ext");
}
static uint64_t rm_captures(const char *path, int depth, int *files) {
  uint64_t freed = 0;
  DIR *d = opendir(path);
  if (!d) return 0;
  struct dirent *e;
  char p[512];
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    snprintf(p, sizeof p, "%s/%s", path, e->d_name);
    struct stat st;
    if (lstat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) { if (depth < 8) { freed += rm_captures(p, depth + 1, files); if (depth >= 1) rmdir(p); } }
    else if (is_capfile(e->d_name) && unlink(p) == 0) { freed += st.st_size; (*files)++; }
  }
  closedir(d);
  return freed;
}

static void cmd_done(const char *key, const char *cid, int ok, int refused, double dl, double cap, int files, const char *why) {
  char body[200], err[120];
  Jn = 0;
  jadd("{\"id\":\"%s\",\"cid\":", console_id); jstr(cid);
  jadd(",\"ok\":%d,\"refused\":%d,\"dlMB\":%.0f,\"capMB\":%.0f,\"files\":%d,\"why\":", ok, refused, dl, cap, files); jstr(why);
  jadd("}");
  char hdr[200];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  for (int i = 0; i < 3 && r2tls_request(ENGINE_HOST, "POST", "/ps4/done", hdr, J, Jn, body, sizeof body, err, sizeof err) != 200; i++) sleep(5);
}
static void clean_up(const char *key, const char *cid) {
  char run[32];
  running_title(run, sizeof run);
  if (run[0] && strcmp(run, VUE_ID)) {
    notify("Round2 clean-up skipped: a game is open. Close it, then press Clean up again.");
    char why[80]; snprintf(why, sizeof why, "a game is open (%s)", run);
    cmd_done(key, cid, 0, 1, 0, 0, 0, why);
    return;
  }
  notify("Round2: cleaning up the PS4 (old update downloads, screenshots and clips)...");
  int files = 0;
  uint64_t dl = 0, cap;
  DIR *d = opendir("/user/download");
  if (d) {
    struct dirent *e;
    char p[300];
    while ((e = readdir(d))) {
      if (!is_title_id(e->d_name) || !strcmp(e->d_name, VUE_ID)) continue; /* PS Vue's download: KEEP */
      snprintf(p, sizeof p, "/user/download/%s", e->d_name);
      dl += rm_files(p, 0, &files, 0);
      rmdir(p);
    }
    closedir(d);
  }
  cap = rm_captures("/user/av_contents/photo", 0, &files) + rm_captures("/user/av_contents/video", 0, &files) +
        rm_files("/user/av_contents/content_tmp", 0, &files, 1);
  char msg[160];
  double tot = (dl + cap) / 1e6;
  if (tot >= 1000) snprintf(msg, sizeof msg, "Round2 clean-up done: %.1f GB freed (update downloads %.1f GB, screenshots/clips %.1f GB)",
                            tot / 1000, dl / 1e9, cap / 1e9);
  else snprintf(msg, sizeof msg, "Round2 clean-up done: %.0f MB freed (update downloads %.0f MB, screenshots/clips %.0f MB)",
                tot, dl / 1e6, cap / 1e6);
  notify(msg);
  cmd_done(key, cid, 1, 0, dl / 1e6, cap / 1e6, files, "");
}
static void jget(const char *b, const char *k, char *out, size_t n) { /* "k":"value" from a small JSON reply */
  char pat[40];
  snprintf(pat, sizeof pat, "\"%s\":", k);
  out[0] = 0;
  const char *p = strstr(b, pat);
  if (!p) return;
  p += strlen(pat);
  if (*p == '"') p++;
  size_t i = 0;
  while (p[i] && p[i] != '"' && p[i] != ',' && p[i] != '}' && i < n - 1) { out[i] = p[i]; i++; }
  out[i] = 0;
}
/* 1.0.7: stepping aside instead of exiting. On Max (13 Nov) the old helper's _exit after an update ended in stop signal 12 and
   apparently left it half-closed still holding the lock, so the starter never swapped. Now: let go of the lock and sleep for
   good (a few MB until the next real restart); the starter takes over. Used for updates, the starter's stop, and stop signals. */
static int g_lock = -1;
static void step_aside(void) {
  if (g_lock >= 0) { flock(g_lock, LOCK_UN); close(g_lock); g_lock = -1; }
  for (;;) sleep(3600);
}
static void put_file(const char *p, const char *txt) { FILE *f = fopen(p, "w"); if (f) { fputs(txt, f); fclose(f); } }
static int file_exists(const char *p) { struct stat st; return !stat(p, &st); }
/* update (1.0.4): download the new helper through the engine, check size + SHA-256 + ELF, save it as app/new.elf, tell the
   starter (app/switch) and exit – the starter swaps it in and rolls back by itself if the new one doesn't work */
static void do_update(const char *key, const char *cid, const char *body) {
  char v[24], sha[80], sz[24], run[32];
  jget(body, "v", v, sizeof v);
  jget(body, "sha", sha, sizeof sha);
  jget(body, "size", sz, sizeof sz);
  if (!file_exists(APP "/boot.ver")) { cmd_done(key, cid, 0, 1, 0, 0, 0, "no starter: put round2-boot.elf in AutoRun first"); return; }
  running_title(run, sizeof run);
  if (run[0] && strcmp(run, VUE_ID)) { cmd_done(key, cid, 0, 1, 0, 0, 0, "a game is open"); return; }
  if (!v[0] || strlen(sha) != 64 || atoi_safe(sz) < 1024) { cmd_done(key, cid, 0, 0, 0, 0, 0, "bad update details"); return; }
  if (!strcmp(v, R2_VERSION)) { cmd_done(key, cid, 1, 0, 0, 0, 0, "already on this version"); return; }
  char msg[160], hdr[200], req[120], err[160];
  snprintf(msg, sizeof msg, "Round2 helper: downloading %s...", v);
  notify(msg);
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  int n = snprintf(req, sizeof req, "{\"id\":\"%s\",\"v\":\"%s\"}", console_id, v);
  unsigned char *d = 0;
  size_t len = 0;
  int st = r2tls_fetch(ENGINE_HOST, "POST", "/ps4/app", hdr, req, n, &d, &len, 16 * 1024 * 1024, err, sizeof err);
  char why[200] = "";
  unsigned char h[32];
  char hx[65];
  if (st != 200 || !d) snprintf(why, sizeof why, "download failed (%d %s)", st, err);
  else if (len != (size_t)atoi_safe(sz)) snprintf(why, sizeof why, "download size %zu, expected %s", len, sz);
  else {
    mbedtls_sha256(d, len, h, 0);
    for (int i = 0; i < 32; i++) sprintf(hx + i * 2, "%02x", h[i]);
    if (strcasecmp(hx, sha)) snprintf(why, sizeof why, "file check failed (SHA-256)");
    else if (memcmp(d, "\x7f" "ELF", 4)) snprintf(why, sizeof why, "not a PS4 program");
  }
  if (!why[0]) {
    FILE *f = fopen(APP "/new.elf.tmp", "wb");
    if (!f || fwrite(d, 1, len, f) != len) snprintf(why, sizeof why, "couldn't save it");
    if (f) { fflush(f); fsync(fileno(f)); fclose(f); }
    if (!why[0] && rename(APP "/new.elf.tmp", APP "/new.elf")) snprintf(why, sizeof why, "couldn't save it (rename)");
  }
  free(d);
  if (why[0]) { unlink(APP "/new.elf.tmp"); cmd_done(key, cid, 0, 0, 0, 0, 0, why); notify("Round2 helper: update not done - still on the old version."); return; }
  snprintf(why, sizeof why, "downloaded %s - switching", v);
  cmd_done(key, cid, 1, 0, 0, 0, 0, why);
  put_file(APP "/switch", v);
  snprintf(msg, sizeof msg, "Round2 helper: switching to %s", v);
  notify(msg);
  step_aside(); /* lock released -> the starter swaps it in (1.0.6 and older exited here) */
}

/* look (1.0.5): read-only listing of folders for staff/Claude (names, sizes, dates) – e.g. what the Capture Gallery keeps after a
   clean-up. Changes nothing. Sent to /ps4/done as "list". */
static int look_n;
static void look_dir(const char *path, int depth, int maxdepth) {
  DIR *d = opendir(path);
  if (!d) { if (look_n++ < 400) { jadd("%s", look_n > 1 ? "," : ""); jstr(path); jadd(":\"(can't open)\""); } return; }
  struct dirent *e;
  char p[512];
  while ((e = readdir(d)) && look_n < 400) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    snprintf(p, sizeof p, "%s/%s", path, e->d_name);
    struct stat st;
    if (lstat(p, &st)) continue;
    char v[64];
    if (S_ISDIR(st.st_mode)) snprintf(v, sizeof v, "dir");
    else snprintf(v, sizeof v, "%lld %lld", (long long)st.st_size, (long long)st.st_mtime);
    jadd("%s", look_n++ ? "," : ""); jstr(p); jadd(":"); jstr(v);
    if (S_ISDIR(st.st_mode) && depth < maxdepth) look_dir(p, depth + 1, maxdepth);
  }
  closedir(d);
}
/* 1.0.9: read-only profile probe ("/_profiles" in a look job) – owner 13 Nov: members get their own PS4 profiles; first find
   where a profile lives. Reads the PS4's profile list and, per used profile slot, every setting in that slot's block of the
   settings store (0x07800000 + (slot-1)*0x10000, step 0x100 – Apollo's name/account-ID/login keys are in there). Changes nothing. */
static void look_kv(const char *k, const char *v) { if (look_n++ < 400) { jadd("%s", look_n > 1 ? "," : ""); jstr(k); jadd(":"); jstr(v); } }
static void look_profiles(void) {
  char k[64], v[96], nm[64];
  RegUserIdList L;
  memset(&L, 0xff, sizeof L);
  int r = sceUserServiceGetRegisteredUserIdList(&L);
  snprintf(v, sizeof v, "%d", r); look_kv("users/registered-call", v);
  for (int i = 0; i < 16; i++) {
    if (L.userId[i] == -1 || L.userId[i] == 0) continue;
    nm[0] = 0; sceUserServiceGetUserName(L.userId[i], nm, sizeof nm);
    snprintf(k, sizeof k, "users/%08x", (unsigned)L.userId[i]); look_kv(k, nm);
  }
  int32_t iu = -1; sceUserServiceGetInitialUser(&iu);
  snprintf(v, sizeof v, "%08x", (unsigned)iu); look_kv("users/initial", v);
  static const long G[] = { 0x07010000, 0x07020000, 0x07030000, 0x07050000, 0x07060000, 0x07070000, 0x07400000, 0x07410000 };
  static const char *GN[] = { "auto_login_user", "max_used_home_u", "init_user", "max_used_guest", "max_used_sharep", "cumulative_user", "disp_acc_info", "new_user_group" };
  for (int i = 0; i < 8; i++) { int x = 0; if (sceRegMgrGetInt(G[i], &x) == 0) { snprintf(k, sizeof k, "reg/USER/%s", GN[i]); snprintf(v, sizeof v, "i:%d (%08x)", x, (unsigned)x); look_kv(k, v); } }
  for (int sl = 1; sl <= 16; sl++) {
    long base = 0x07800000L + (long)(sl - 1) * 0x10000L;
    char name[64] = "";
    sceRegMgrGetStr(base + 0x200, name, 17);
    if (!name[0]) { snprintf(k, sizeof k, "reg/slot%02d", sl); look_kv(k, "(no name)"); continue; }
    for (long off = 0; off < 0x10000; off += 0x100) {
      long kk = base + off;
      int x = 0;
      unsigned char b[16];
      char str[72];
      if (sceRegMgrGetInt(kk, &x) == 0) { if (!x) continue; snprintf(v, sizeof v, "i:%d (%08x)", x, (unsigned)x); }
      else if ((str[0] = 0, sceRegMgrGetStr(kk, str, 65)) == 0) { if (!str[0]) continue; snprintf(v, sizeof v, "s:%.40s", str); }
      else if (memset(b, 0, sizeof b), sceRegMgrGetBin(kk, b, 8) == 0) {
        int nz = 0; for (int j = 0; j < 8; j++) nz |= b[j];
        if (!nz) continue;
        snprintf(v, sizeof v, "b:%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
      } else continue;
      snprintf(k, sizeof k, "reg/slot%02d/%04lx", sl, off); look_kv(k, v);
    }
    { char np[24] = ""; if (sceRegMgrGetStr(base + 0xB007, np, 17) == 0 && np[0]) { snprintf(k, sizeof k, "reg/slot%02d/b007", sl); snprintf(v, sizeof v, "s:%s", np); look_kv(k, v); } }
  }
}

/* 1.1.0: profile write test (staff job "prof", owner 13 Nov: test on Kai). One change per job, read back before and after:
   op "acct"   u=<user id hex> v=<16 hex bytes>  – offline-activate with that account ID (the 3 settings Apollo writes)
   op "unacct" u=<user id hex>                   – undo: account ID zeroed, login flag back to 2, "np" cleared
   op "name"   u=<user id hex> v=<new name>      – rename with the PS4's own function (not for a signed-in profile)
   Refused while a game is open. Nothing else is touched. */
static long prof_slot_base(uint32_t uid) {
  for (int sl = 1; sl <= 16; sl++) {
    long base = 0x07800000L + (long)(sl - 1) * 0x10000L;
    int x = 0;
    if (sceRegMgrGetInt(base + 0x100, &x) == 0 && (uint32_t)x == uid) return base;
  }
  return 0;
}
static void prof_snap(const char *tag, uint32_t uid, long base) {
  char k[64], v[96], nm[40] = "", rn[40] = "", np[24] = "", ud[40] = "";
  unsigned char b[8] = { 0 };
  int lf = -99;
  sceUserServiceGetUserName((int32_t)uid, nm, sizeof nm);
  sceRegMgrGetStr(base + 0x200, rn, 17);
  int rb = sceRegMgrGetBin(base + 0x500, b, 8);
  sceRegMgrGetInt(base + 0x800, &lf);
  sceRegMgrGetStr(base + 0xB007, np, 17);
  char p[80];
  snprintf(p, sizeof p, "/user/home/%08x/username.dat", uid);
  FILE *f = fopen(p, "rb");
  if (f) { size_t n = fread(ud, 1, 17, f); ud[n < 39 ? n : 39] = 0; fclose(f); }
  snprintf(k, sizeof k, "%s/name(service)", tag); look_kv(k, nm);
  snprintf(k, sizeof k, "%s/name(setting)", tag); look_kv(k, rn);
  snprintf(k, sizeof k, "%s/username.dat", tag); look_kv(k, ud);
  snprintf(k, sizeof k, "%s/account", tag);
  if (rb) snprintf(v, sizeof v, "(none, %d)", rb); else snprintf(v, sizeof v, "%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
  look_kv(k, v);
  snprintf(k, sizeof k, "%s/login_flag", tag); snprintf(v, sizeof v, "%d", lf); look_kv(k, v);
  snprintf(k, sizeof k, "%s/np", tag); look_kv(k, np);
}
static void prof_logins(const char *tag) { /* who is signed in, by name */
  UserIdList l;
  char v[200] = "", nm[40];
  memset(&l, 0, sizeof l);
  int r = sceUserServiceGetLoginUserIdList(&l);
  for (int i = 0; i < 4; i++) {
    if (l.userId[i] == -1 || l.userId[i] == 0) continue;
    nm[0] = 0; sceUserServiceGetUserName(l.userId[i], nm, sizeof nm);
    size_t n = strlen(v); snprintf(v + n, sizeof v - n, "%s%s(%08x)", n ? ", " : "", nm, (unsigned)l.userId[i]);
  }
  if (r) snprintf(v + strlen(v), sizeof v - strlen(v), " [call %d]", r);
  char k[40]; snprintf(k, sizeof k, "%s/signed_in", tag); look_kv(k, v[0] ? v : "(nobody)");
}
static void do_prof(const char *key, const char *cid, const char *body) {
  char op[16], us[16], val[40], run[32], why[160] = "";
  jget(body, "op", op, sizeof op);
  jget(body, "u", us, sizeof us);
  jget(body, "v", val, sizeof val);
  running_title(run, sizeof run);
  if (run[0] && strcmp(run, VUE_ID)) { cmd_done(key, cid, 0, 1, 0, 0, 0, "a game is open"); return; }
  uint32_t uid = (uint32_t)strtoul(us, 0, 16);
  long base = uid ? prof_slot_base(uid) : 0;
  if (!base) { cmd_done(key, cid, 0, 0, 0, 0, 0, "no profile with that user id"); return; }
  Jn = 0;
  look_n = 0;
  jadd("{\"id\":\"%s\",\"cid\":", console_id); jstr(cid);
  jadd(",\"list\":{");
  prof_snap("before", uid, base);
  prof_logins("before");
  int r = -1;
  int mainu = 0;
  sceRegMgrGetInt(0x07010000, &mainu); /* the auto-login (main) profile – never signed out */
  if (!strcmp(op, "acct") && strlen(val) == 16) {
    unsigned char b[8];
    for (int i = 0; i < 8; i++) { char h[3] = { val[i * 2], val[i * 2 + 1], 0 }; b[i] = (unsigned char)strtoul(h, 0, 16); }
    r = sceRegMgrSetBin(base + 0x500, b, 8);
    r |= sceRegMgrSetStr(base + 0xB007, "np", 3);
    r |= sceRegMgrSetInt(base + 0x800, 6);
    snprintf(why, sizeof why, "acct set: %d", r);
  } else if (!strcmp(op, "unacct")) {
    unsigned char z[8] = { 0 };
    r = sceRegMgrSetBin(base + 0x500, z, 8);
    r |= sceRegMgrSetInt(base + 0x800, 2);
    r |= sceRegMgrSetStr(base + 0xB007, "", 1);
    snprintf(why, sizeof why, "acct cleared: %d", r);
  } else if (!strcmp(op, "name") && val[0] && strlen(val) <= 16) {
    UserIdList l;
    memset(&l, 0, sizeof l);
    sceUserServiceGetLoginUserIdList(&l);
    for (int i = 0; i < 4; i++) if ((uint32_t)l.userId[i] == uid) { snprintf(why, sizeof why, "profile is signed in - not renamed"); break; }
    if (!why[0]) { r = sceUserServiceSetUserName((int32_t)uid, val); snprintf(why, sizeof why, "rename: %d (0x%08x)", r, (unsigned)r); }
  } else if (!strcmp(op, "logout")) { /* 1.1.1 test: sign a spare profile out */
    if ((uint32_t)mainu == uid) snprintf(why, sizeof why, "that is the main profile - never signed out");
    else { r = sceUserServiceLogout((int32_t)uid); sleep(4); snprintf(why, sizeof why, "logout: %d (0x%08x)", r, (unsigned)r); }
  } else if (!strcmp(op, "login")) { /* 1.1.1 test: sign a profile in */
    r = sceUserServiceLogin((int32_t)uid); sleep(4); snprintf(why, sizeof why, "login: %d (0x%08x)", r, (unsigned)r);
  } else snprintf(why, sizeof why, "unknown op or bad value");
  prof_snap("after", uid, base);
  prof_logins("after");
  jadd("},\"ok\":%d,\"refused\":0,\"why\":", r == 0); jstr(why);
  jadd("}");
  char hdr[200], rbody[200], err[120];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  for (int i = 0; i < 3 && r2tls_request(ENGINE_HOST, "POST", "/ps4/done", hdr, J, Jn, rbody, sizeof rbody, err, sizeof err) != 200; i++) sleep(5);
}

/* 1.1.2: read-only save probe ("/_save.<user id>.<title id>.<save name>|depth" in a look job) – owner 14 Nov: back up members'
   saves decrypted. Apollo's way (apollo-ps4 source/sd.c + orbis_jbc.c): unlock the save's own key (<name>.bin) with the PS4's
   key service, open the save image READ-ONLY in a private folder, list what's inside, close it again. Raises its rights only for
   the job (same as Apollo) and puts them back. Refused while a game is open (the game may have the save open). Changes nothing. */
/* owner 14 Nov 1:44am: Max's Gran Turismo Sport save has everything unlocked (the shop's racing-sim USP, only on Max).
   1:50am: it may be READ (backed up, copied to another profile for a test) – but the copy in the MAIN profile is never written
   to, restored over or deleted. Every write job checks this first. */
static const char *R2_PROTECTED_TITLES[] = { "CUSA02168" /* Gran Turismo Sport */ };
static int r2_protected(const char *tid) {
  for (unsigned i = 0; i < sizeof R2_PROTECTED_TITLES / sizeof *R2_PROTECTED_TITLES; i++) if (!strcmp(tid, R2_PROTECTED_TITLES[i])) return 1;
  return 0;
}
static uint32_t r2_main_user(void) { int m = 0; sceRegMgrGetInt(0x07010000, &m); return (uint32_t)m; }

/* Saves (1.1.2 read-only probe, 1.1.4 backup + restore) – Apollo's way (apollo-ps4 source/sd.c, orbis_jbc.c): unlock the save's
   own key (<save>.bin) with the PS4's key service, open sdimg_<save> in a private folder, close it again. Rights are raised only
   for the job and put back. Every job is refused while a game is open (the game may have the save open). */
typedef struct { bool readOnly; char *budgetid; } R2MountOpt;
typedef struct { bool dummy; } R2UmountOpt;
static int (*fsInitMount)(R2MountOpt *);
static int (*fsMount)(R2MountOpt *, const char *, const char *, uint8_t *);
static int (*fsInitUmount)(R2UmountOpt *);
static int (*fsUmount)(R2UmountOpt *, const char *, int, bool);
static const char SV_MNT[] = "/data/round2/mnt";
static uint64_t sv_old_auth;
static uint8_t sv_old_caps[16];
static int sv_caps_ok;
static void sv_rights_up(char *v, size_t vn) {
  pid_t me = getpid();
  sv_old_auth = kernel_get_ucred_authid(me);
  uint8_t caps[16];
  sv_caps_ok = kernel_get_ucred_caps(me, sv_old_caps) == 0;
  memcpy(caps, sv_old_caps, 16); caps[7] |= 0x40; /* Apollo: sonyCred |= 0x4000000000000000 */
  int ra = kernel_set_ucred_authid(me, 0x3801000000000013ULL), rc = sv_caps_ok ? kernel_set_ucred_caps(me, caps) : -1;
  snprintf(v, vn, "authid %016llx set %d caps %d", (unsigned long long)sv_old_auth, ra, rc);
}
static void sv_rights_back(void) {
  pid_t me = getpid();
  kernel_set_ucred_authid(me, sv_old_auth);
  if (sv_caps_ok) kernel_set_ucred_caps(me, sv_old_caps);
}
static int sv_lib(void) {
  if (fsMount && fsUmount && fsInitMount && fsInitUmount) return 0;
  int m = sceKernelLoadStartModule("/system/priv/lib/libSceFsInternalForVsh.sprx", 0, 0, 0, 0, 0);
  if (m < 0) return m;
  sceKernelDlsym(m, "sceFsInitMountSaveDataOpt", (void **)&fsInitMount); sceKernelDlsym(m, "sceFsMountSaveData", (void **)&fsMount);
  sceKernelDlsym(m, "sceFsInitUmountSaveDataOpt", (void **)&fsInitUmount); sceKernelDlsym(m, "sceFsUmountSaveData", (void **)&fsUmount);
  return fsMount && fsUmount && fsInitMount && fsInitUmount ? 0 : -1;
}
/* open one save at SV_MNT (ro = read-only). why = what went wrong. 0 = open. */
static int sv_open(const char *u, const char *t, const char *nm, int ro, char *why, size_t wn) {
  char img[200], keyp[200];
  struct stat st;
  snprintf(img, sizeof img, "/user/home/%s/savedata/%s/sdimg_%s", u, t, nm);
  snprintf(keyp, sizeof keyp, "/user/home/%s/savedata/%s/%s.bin", u, t, nm);
  if (stat(img, &st) || stat(keyp, &st)) { snprintf(why, wn, "no such save"); return -1; }
  if (sv_lib()) { snprintf(why, wn, "save library missing"); return -1; }
  uint8_t enc[0x90]; /* the key service writes 0x84 bytes (Apollo pads it the same way) */
  memset(enc, 0, sizeof enc);
  int kf = open(keyp, O_RDONLY), kr = kf >= 0 ? (int)read(kf, enc, 0x60) : -1;
  if (kf >= 0) close(kf);
  int sb = open("/dev/sbl_srv", O_RDWR), io = -1;
  if (sb >= 0 && kr == 0x60) { io = ioctl(sb, 0xc0845302, enc); close(sb); }
  if (io != 0) { snprintf(why, wn, "key: file %d, service %s, unlock %d", kr, sb >= 0 ? "ok" : "CAN'T OPEN", io); memset(enc, 0, sizeof enc); return -1; }
  mkdir(SV_MNT, 0777);
  R2MountOpt mo; memset(&mo, 0, sizeof mo); fsInitMount(&mo); mo.readOnly = ro ? true : false; mo.budgetid = "system";
  int mr = fsMount(&mo, img, SV_MNT, enc + 0x60);
  memset(enc, 0, sizeof enc);
  if (mr < 0) { snprintf(why, wn, "open %d (0x%08x)", mr, (unsigned)mr); rmdir(SV_MNT); return -1; }
  return 0;
}
static int sv_close(void) {
  R2UmountOpt uo; memset(&uo, 0, sizeof uo); fsInitUmount(&uo);
  int ur = fsUmount(&uo, SV_MNT, 0, false);
  if (ur < 0) { sleep(2); ur = fsUmount(&uo, SV_MNT, 0, true); }
  rmdir(SV_MNT);
  return ur;
}
static int sv_game_open(void) { char run[32]; running_title(run, sizeof run); return run[0] && strcmp(run, VUE_ID); }

static int look_save(const char *spec, int dp) { /* "/_save.<user>.<title>.<save>" – read-only listing */
  char v[200], u[16] = "", t[16] = "", nm[48] = "";
  if (sscanf(spec, "/_save.%8[0-9a-f].%9[A-Z0-9].%47[^|]", u, t, nm) != 3) { look_kv("save/error", "bad spec"); return -1; }
  if (sv_game_open()) { look_kv("save/refused", "a game is open"); return -1; }
  sv_rights_up(v, sizeof v); look_kv("save/1-rights", v);
  int ok = sv_open(u, t, nm, 1, v, sizeof v);
  if (ok == 0) { look_kv("save/2-open", "read-only"); look_dir(SV_MNT, 0, dp > 4 ? 4 : dp); snprintf(v, sizeof v, "%d", sv_close()); look_kv("save/3-close", v); }
  else look_kv("save/2-open", v);
  sv_rights_back();
  return ok;
}

/* ---- 1.1.4 backup: every file of an opened save goes to the engine (/ps4/sput -> Cloudflare storage "round2-saves"),
   1 MB pieces, then a manifest ("<file>\t<size>\t<pieces>" per line). Stored under <console>/<user>/<title>/<save>/<stamp>/. */
#define SV_PIECE (1u << 20) /* 1 MB: the helper's memory is small (8 MB failed on Max; 1.1 MB updates work) */
static void url_enc(char *o, size_t on, const char *s) {
  static const char hx[] = "0123456789ABCDEF";
  size_t n = 0;
  for (; *s && n + 4 < on; s++) {
    unsigned char c = (unsigned char)*s;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/') o[n++] = (char)c;
    else { o[n++] = '%'; o[n++] = hx[c >> 4]; o[n++] = hx[c & 15]; }
  }
  o[n] = 0;
}
static int sv_put(const char *key, const char *k, const unsigned char *data, size_t n) {
  char path[900], hdr[200], out[200], err[120];
  int m = snprintf(path, sizeof path, "/ps4/sput?k=");
  url_enc(path + m, sizeof path - m, k);
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/octet-stream\r\n", key);
  for (int i = 0; i < 3; i++) { if (r2tls_request(ENGINE_HOST, "POST", path, hdr, (const char *)data, n, out, sizeof out, err, sizeof err) == 200) return 0; sleep(3); }
  return -1;
}
static char *sv_man; static size_t sv_man_n, sv_man_cap;
static unsigned char *sv_buf;
static int sv_files, sv_fail; static unsigned long long sv_bytes;
static void sv_man_add(const char *line) {
  size_t l = strlen(line);
  if (sv_man_n + l + 1 > sv_man_cap) { size_t c = (sv_man_cap ? sv_man_cap * 2 : 16384) + l; char *x = realloc(sv_man, c); if (!x) return; sv_man = x; sv_man_cap = c; }
  memcpy(sv_man + sv_man_n, line, l); sv_man_n += l; sv_man[sv_man_n] = 0;
}
static void sv_up_dir(const char *key, const char *base, const char *dir, const char *rel, int depth) {
  DIR *d = opendir(dir);
  if (!d) { sv_fail++; return; }
  struct dirent *e;
  char p[512], r[400], k[700], line[520];
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
    struct stat st;
    if (lstat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) { if (depth < 8) sv_up_dir(key, base, p, r, depth + 1); continue; }
    if (!S_ISREG(st.st_mode)) continue;
    int fd = open(p, O_RDONLY);
    if (fd < 0) { sv_fail++; continue; }
    unsigned long long sz = (unsigned long long)st.st_size, done = 0;
    int pieces = 0, bad = 0;
    do {
      size_t want = sz - done > SV_PIECE ? SV_PIECE : (size_t)(sz - done), got = 0;
      while (got < want) { ssize_t x = read(fd, sv_buf + got, want - got); if (x <= 0) break; got += (size_t)x; }
      if (got != want) { bad = 1; break; }
      snprintf(k, sizeof k, "%s/f/%s~%d", base, r, pieces);
      if (sv_put(key, k, sv_buf, got)) { bad = 1; break; }
      done += got; pieces++;
    } while (done < sz);
    close(fd);
    if (bad) { sv_fail++; continue; }
    sv_files++; sv_bytes += sz;
    snprintf(line, sizeof line, "%s\t%llu\t%d\n", r, sz, pieces);
    sv_man_add(line);
  }
  closedir(d);
}
static int look_sback(const char *key, const char *spec) { /* "/_sback.<user>.<title>.<save>" */
  char v[200], u[16] = "", t[16] = "", nm[48] = "", base[200];
  if (sscanf(spec, "/_sback.%8[0-9a-f].%9[A-Z0-9].%47[^|]", u, t, nm) != 3) { look_kv("backup/error", "bad spec"); return -1; }
  if (sv_game_open()) { look_kv("backup/refused", "a game is open"); return -1; }
  if (!sv_buf) { void *b = mmap(0, SV_PIECE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (b == MAP_FAILED) { look_kv("backup/error", "no memory"); return -1; } sv_buf = b; }
  sv_rights_up(v, sizeof v);
  int ok = sv_open(u, t, nm, 1, v, sizeof v);
  if (ok == 0) {
    snprintf(base, sizeof base, "%s/%s/%s/%s/%lld", console_id, u, t, nm, (long long)time(0));
    sv_files = sv_fail = 0; sv_bytes = 0; sv_man_n = 0; if (sv_man) sv_man[0] = 0;
    sv_up_dir(key, base, SV_MNT, "", 0);
    int cr = sv_close();
    char mk[260]; snprintf(mk, sizeof mk, "%s/manifest.txt", base);
    int mr = sv_fail ? -1 : sv_put(key, mk, (const unsigned char *)(sv_man ? sv_man : ""), sv_man_n);
    { char kk[80]; snprintf(kk, sizeof kk, "backup/%s/where", nm); look_kv(kk, base); }
    snprintf(v, sizeof v, "%d files, %llu bytes, %d failed, manifest %s, closed %d", sv_files, sv_bytes, sv_fail, mr ? "NOT saved" : "saved", cr);
    { char kk[80]; snprintf(kk, sizeof kk, "backup/%s/result", nm); look_kv(kk, v); }
    ok = sv_fail || mr ? -1 : 0;
  } else look_kv("backup/error", v);
  sv_rights_back();
  return ok;
}

/* ---- 1.1.4 restore: copy a backup's game files INTO an existing save of the same game in another profile (owner 14 Nov:
   Max's GT Sport -> User4's GT Sport, so he can see it on the TV). The receiving save keeps its own sce_sys (param.sfo with
   that profile's IDs, keystone) – only the game's files are replaced. Never into the main profile's protected save. The
   receiving save should be backed up first (look_sback). */
static int sv_get(const char *key, const char *k, unsigned char **data, size_t *len, char *why, size_t wn) {
  char path[900], hdr[200], err[120];
  int m = snprintf(path, sizeof path, "/ps4/sget?k=");
  url_enc(path + m, sizeof path - m, k);
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\n", key);
  int st = -1;
  for (int i = 0; i < 3; i++) { *data = 0; *len = 0; err[0] = 0; st = r2tls_fetch(ENGINE_HOST, "GET", path, hdr, 0, 0, data, len, SV_PIECE + 65536, err, sizeof err); if (st == 200) return 0; free(*data); *data = 0; sleep(3); }
  if (why) snprintf(why, wn, "download %d %s", st, err);
  return -1;
}
static void mkdirs_for(const char *file) {
  char d[512]; snprintf(d, sizeof d, "%s", file);
  for (char *q = d + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(d, 0777); *q = '/'; }
}
static int look_srest(const char *key, const char *spec) { /* "/_srest.<user>.<title>.<save>.<backup path>" */
  char v[220], u[16] = "", t[16] = "", nm[48] = "", src[200];
  if (sscanf(spec, "/_srest.%8[0-9a-f].%9[A-Z0-9].%47[^.].%199[^|]", u, t, nm, src) != 4) { look_kv("restore/error", "bad spec"); return -1; }
  if (r2_protected(t) && (uint32_t)strtoul(u, 0, 16) == r2_main_user()) { look_kv("restore/refused", "protected game in the main profile - never written (owner)"); return -1; }
  if (sv_game_open()) { look_kv("restore/refused", "a game is open"); return -1; }
  if (strstr(src, "..") || !strstr(src, t)) { look_kv("restore/error", "backup is for another game"); return -1; }
  char mk[260]; snprintf(mk, sizeof mk, "%s/manifest.txt", src);
  unsigned char *man = 0; size_t mlen = 0;
  if (sv_get(key, mk, &man, &mlen, 0, 0) || !mlen) { free(man); look_kv("restore/error", "backup not found"); return -1; }
  char *mt = malloc(mlen + 1); memcpy(mt, man, mlen); mt[mlen] = 0; free(man);
  sv_rights_up(v, sizeof v);
  char first_err[160] = "";
  if (!sv_buf) { void *b = mmap(0, SV_PIECE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (b == MAP_FAILED) { look_kv("restore/error", "no memory"); free(mt); return -1; } sv_buf = b; }
  int ok = sv_open(u, t, nm, 0, v, sizeof v), files = 0, fail = 0, skipped = 0;
  unsigned long long bytes = 0;
  if (ok == 0) {
    for (char *ln = mt, *nx; ln && *ln; ln = nx) {
      nx = strchr(ln, '\n'); if (nx) *nx++ = 0;
      char *t1 = strchr(ln, '\t'); if (!t1) continue; *t1++ = 0;
      char *t2 = strchr(t1, '\t'); if (!t2) continue; *t2++ = 0;
      unsigned long long sz = strtoull(t1, 0, 10); int pieces = atoi_safe(t2);
      if (!strncmp(ln, "sce_sys/", 8) || strstr(ln, "..")) { skipped++; continue; } /* the receiving save keeps its own labels */
      char out[512], tmp[520], k[700];
      /* 1.1.7: download the whole file to the PS4's own storage first (the save has no room for a 2nd copy – 32 MB GT Sport
         memory.dat failed at 3 MB), check it's complete, only then write it into the save in one go. */
      snprintf(out, sizeof out, "%s/%s", SV_MNT, ln); snprintf(tmp, sizeof tmp, "/data/round2/stage/%s", ln);
      mkdirs_for(out); mkdirs_for(tmp);
      int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666), bad = fd < 0;
      char why[160] = "";
      if (bad) snprintf(why, sizeof why, "%s: can't create (errno %d)", ln, errno);
      unsigned long long got = 0;
      for (int pi = 0; !bad && pi < pieces; pi++) {
        unsigned char *d = 0; size_t dl = 0;
        char gw[130] = "";
        snprintf(k, sizeof k, "%s/f/%s~%d", src, ln, pi);
        if (sv_get(key, k, &d, &dl, gw, sizeof gw)) { bad = 1; free(d); snprintf(why, sizeof why, "%s piece %d: %s", ln, pi, gw); break; }
        ssize_t wr = write(fd, d, dl);
        if (wr != (ssize_t)dl) { bad = 1; snprintf(why, sizeof why, "%s piece %d: write %zd of %zu (errno %d)", ln, pi, wr, dl, errno); }
        got += dl; free(d);
      }
      if (fd >= 0) { fsync(fd); close(fd); }
      if (!bad && got != sz) { bad = 1; snprintf(why, sizeof why, "%s: got %llu of %llu bytes", ln, got, sz); }
      if (bad) { unlink(tmp); fail++; if (!first_err[0]) snprintf(first_err, sizeof first_err, "%s", why); continue; }
      { /* copy the complete staged file into the save */
        int in = open(tmp, O_RDONLY), of = in >= 0 ? open(out, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
        unsigned long long cp = 0; int cbad = in < 0 || of < 0, cerr = cbad ? errno : 0;
        while (!cbad) { ssize_t x = read(in, sv_buf, SV_PIECE); if (x < 0) { cbad = 1; cerr = errno; break; } if (x == 0) break;
          if (write(of, sv_buf, (size_t)x) != x) { cbad = 1; cerr = errno; break; } cp += (unsigned long long)x; }
        if (of >= 0) { fsync(of); close(of); }
        if (in >= 0) close(in);
        unlink(tmp);
        if (cbad || cp != sz) { if (!first_err[0]) snprintf(first_err, sizeof first_err, "%s: copy into save %llu of %llu (errno %d)", ln, cp, sz, cerr); fail++; continue; }
      }
      files++; bytes += sz;
    }
    int cr = sv_close();
    snprintf(v, sizeof v, "%d files, %llu bytes, %d failed, %d kept (sce_sys), closed %d", files, bytes, fail, skipped, cr);
    { char kk[80]; snprintf(kk, sizeof kk, "restore/%s/result", nm); look_kv(kk, v); if (first_err[0]) { snprintf(kk, sizeof kk, "restore/%s/first-problem", nm); look_kv(kk, first_err); } }
    ok = fail ? -1 : 0;
  } else look_kv("restore/error", v);
  sv_rights_back();
  free(mt);
  return ok;
}
static void do_look(const char *key, const char *cid, const char *body) {
  Jn = 0;
  look_n = 0;
  jadd("{\"id\":\"%s\",\"cid\":", console_id); jstr(cid);
  jadd(",\"ok\":1,\"refused\":0,\"why\":\"listing\",\"list\":{");
  char ps[400];
  jget(body, "p", ps, sizeof ps); /* 1.0.6: "/dir|depth;/dir2|depth" (folders to list), else the capture folders */
  if (!ps[0]) { look_dir("/user/av_contents", 0, 4); look_dir("/system_data/priv/mms", 0, 0); }
  for (char *t = ps, *nx; t && *t; t = nx) {
    nx = strchr(t, ';');
    if (nx) *nx++ = 0;
    char *bar = strchr(t, '|');
    int dp = 0;
    if (bar) { *bar = 0; dp = atoi_safe(bar + 1); }
    if (!strcmp(t, "/_profiles")) look_profiles();
    else if (!strncmp(t, "/_save.", 7)) look_save(t, dp);
    else if (!strncmp(t, "/_sback.", 8)) look_sback(key, t);
    else if (!strncmp(t, "/_srest.", 8)) look_srest(key, t);
    else if (t[0] == '/' && !strstr(t, "..")) look_dir(t, 0, dp > 6 ? 6 : dp);
  }
  jadd("}}");
  char hdr[200], rbody[200], err[120];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  for (int i = 0; i < 3 && r2tls_request(ENGINE_HOST, "POST", "/ps4/done", hdr, J, Jn, rbody, sizeof rbody, err, sizeof err) != 200; i++) sleep(5);
}

static int check_cmd(const char *key) { /* every minute; 1 = it did something (report again) */
  char hdr[200], body[400], err[120], req[80];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  int n = snprintf(req, sizeof req, "{\"id\":\"%s\"}", console_id);
  if (r2tls_request(ENGINE_HOST, "POST", "/ps4/cmd", hdr, req, n, body, sizeof body, err, sizeof err) != 200) return 0;
  char *c = strstr(body, "\"cid\":\""), cid[24] = "";
  if (!c) return 0;
  c += 7;
  int i = 0;
  while (c[i] && c[i] != '"' && i < 23) { cid[i] = c[i]; i++; }
  cid[i] = 0;
  if (strstr(body, "\"do\":\"clean\"")) clean_up(key, cid);
  else if (strstr(body, "\"do\":\"update\"")) do_update(key, cid, body);
  else if (strstr(body, "\"do\":\"look\"")) do_look(key, cid, body);
  else if (strstr(body, "\"do\":\"prof\"")) do_prof(key, cid, body);
  else cmd_done(key, cid, 0, 1, 0, 0, 0, "this helper doesn't know that command - update it");
  return 1;
}

static void write_status(const char *line) {
  FILE *f = fopen("/data/round2/helper.txt", "w");
  if (!f) return;
  time_t t = time(0) + 8 * 3600; /* display only – may be wrong if the PS4 clock is */
  struct tm tm;
  gmtime_r(&t, &tm);
  fprintf(f, "Round2 helper %s\nconsole %s\nlast report (PS4 clock %02d/%02d %02d:%02d): %s\n", R2_VERSION, console_id, tm.tm_mday,
          tm.tm_mon + 1, tm.tm_hour, tm.tm_min, line);
  fclose(f);
}

static void on_sig(int sig) {
  FILE *f = fopen("/data/round2/helper-stopped.txt", "a");
  if (f) {
    time_t t = time(0) + 8 * 3600;
    struct tm tm;
    gmtime_r(&t, &tm);
    fprintf(f, "%02d/%02d %02d:%02d:%02d helper %s stopped by signal %d\n", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec, R2_VERSION, sig);
    fclose(f);
  }
  signal(sig, SIG_IGN);
  step_aside(); /* never _exit (see step_aside); the starter starts a fresh one */
}

static int read_key(char *key, size_t n) {
  FILE *f = fopen("/data/round2/key.txt", "r");
  if (!f) return 0;
  key[0] = 0;
  if (!fgets(key, n, f)) key[0] = 0;
  fclose(f);
  key[strcspn(key, "\r\n \t")] = 0;
  return key[0] != 0;
}

int main(void) {
  mkdir("/data/round2", 0777);
  mkdir(APP, 0777);
  /* only one helper at a time: hold the lock while running (the starter watches it). Another one has it -> leave quietly. */
  int lk = open(LOCKF, O_RDWR | O_CREAT, 0666), got = 0;
  for (int i = 0; i < 10 && lk >= 0 && !(got = flock(lk, LOCK_EX | LOCK_NB) == 0); i++) sleep(1);
  if (lk >= 0 && !got) for (;;) sleep(3600); /* another helper runs: stay out of the way (returning = exiting, see step_aside) */
  g_lock = lk;
  put_file(APP "/hello", R2_VERSION); /* the starter keeps a new version once it says hello */
  signal(SIGPIPE, SIG_IGN); /* a dropped connection must never stop the helper */
  for (int sg = 1; sg < 32; sg++) if (sg != SIGKILL && sg != SIGSTOP && sg != SIGPIPE && sg != SIGALRM && sg != SIGCHLD) signal(sg, on_sig);
  sceUserServiceInitialize(0);
  sceNetCtlInit();
  get_console_id();
  char key[128] = "", status[400] = "starting";
  int warned = 0;
  write_status("started, first report in about 10 seconds");
  update_guard();
  for (unsigned long m = 0;; m++) {
    char run[32];
    running_title(run, sizeof run);
    note_playing(run);
    FILE *a = fopen("/data/round2/alive", "w"); /* for the starter (later) */
    if (a) { fprintf(a, "%lu\n", m); fclose(a); }
    if (m % REPORT_EVERY == 0) { /* 1.0.5: first report ~10 s after start (was 1 min), retried while the network settles */
      if (!read_key(key, sizeof key)) {
        write_status("no key – put the PS4 key in /data/round2/key.txt");
        if (!warned++) notify("Round2 helper: key missing (/data/round2/key.txt)");
      } else {
        if (m == 0) sleep(10);
        int st = report(key, status, sizeof status);
        for (int i = 0; m == 0 && st != 200 && i < 6; i++) { sleep(8); st = report(key, status, sizeof status); }
        write_status(status);
        if (m == 0) {
          char msg[200];
          snprintf(msg, sizeof msg, "Round2 helper %s: %s", R2_VERSION, st == 200 ? "connected" : "can't reach Round2 (will retry)");
          notify(msg);
        }
      }
    }
    if ((key[0] || read_key(key, sizeof key)) && check_cmd(key)) { report(key, status, sizeof status); write_status(status); }
    for (int t = 0; t < 30; t++) { /* one minute = 30 x 2 s; the update guard runs every 2 s */
      if (file_exists(APP "/stop")) step_aside(); /* the starter asked (roll-back) */
      if (update_guard()) {
        notify("Round2 stopped a PS4 system update. Please don't accept update prompts.");
        if (read_key(key, sizeof key)) { report(key, status, sizeof status); write_status(status); } /* tell the engine now */
      }
      sleep(2);
    }
  }
  return 0;
}
