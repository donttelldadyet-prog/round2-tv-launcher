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
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include "mbedtls/sha256.h"

#include "r2tls.h"

#define R2_VERSION "1.0.4"
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
int sceUserServiceInitialize(void *);
typedef struct { int32_t userId[4]; } UserIdList;
int sceUserServiceGetLoginUserIdList(UserIdList *);
int sceUserServiceGetUserName(int32_t, char *, size_t);
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
  cap = rm_files("/user/av_contents", 0, &files, 1);
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
  _exit(0); /* lock released -> the starter swaps it in */
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
  if (f) { fprintf(f, "stopped by signal %d\n", sig); fclose(f); }
  _exit(0);
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
  if (lk >= 0 && !got) return 0;
  put_file(APP "/hello", R2_VERSION); /* the starter keeps a new version once it says hello */
  signal(SIGPIPE, SIG_IGN); /* a dropped connection must never stop the helper */
  for (int sg = 1; sg < 32; sg++) if (sg != SIGKILL && sg != SIGSTOP && sg != SIGPIPE && sg != SIGALRM && sg != SIGCHLD) signal(sg, on_sig);
  sceUserServiceInitialize(0);
  sceNetCtlInit();
  get_console_id();
  char key[128] = "", status[400] = "starting";
  int warned = 0;
  write_status("started, first report in 1 minute");
  update_guard();
  for (unsigned long m = 0;; m++) {
    char run[32];
    running_title(run, sizeof run);
    note_playing(run);
    FILE *a = fopen("/data/round2/alive", "w"); /* for the starter (later) */
    if (a) { fprintf(a, "%lu\n", m); fclose(a); }
    if (m % REPORT_EVERY == 1) {
      if (!read_key(key, sizeof key)) {
        write_status("no key – put the PS4 key in /data/round2/key.txt");
        if (!warned++) notify("Round2 helper: key missing (/data/round2/key.txt)");
      } else {
        int st = report(key, status, sizeof status);
        write_status(status);
        if (m == 1) {
          char msg[200];
          snprintf(msg, sizeof msg, "Round2 helper %s: %s", R2_VERSION, st == 200 ? "connected" : "can't reach Round2 (will retry)");
          notify(msg);
        }
      }
    }
    if ((key[0] || read_key(key, sizeof key)) && check_cmd(key)) { report(key, status, sizeof status); write_status(status); }
    for (int t = 0; t < 30; t++) { /* one minute = 30 x 2 s; the update guard runs every 2 s */
      if (file_exists(APP "/stop")) _exit(0); /* the starter asked (roll-back) */
      if (update_guard()) {
        notify("Round2 stopped a PS4 system update. Please don't accept update prompts.");
        if (read_key(key, sizeof key)) { report(key, status, sizeof status); write_status(status); } /* tell the engine now */
      }
      sleep(2);
    }
  }
  return 0;
}
