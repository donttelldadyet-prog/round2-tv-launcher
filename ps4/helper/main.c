/* Round2 PS4 helper (round2d) v1 – check-ins.
   Runs in the background after the jailbreak (GoldHEN AutoRun). Every minute it notes which game is running (play time);
   every 15 minutes it reports to the Round2 booking engine: console ID, firmware, temperature, disk, Wi-Fi, who's signed in,
   Sony blocked?, installed games (only when the list changed), stuck downloads (never PS Vue's), captures, crashes,
   minutes played per game. (Controllers are not touched – see add_users.) It changes nothing on the PS4.
   Key: /data/round2/key.txt (one line, the engine's PS4_KEY). Status for staff: /data/round2/helper.txt.
   GPL-3.0. Built with ps4-payload-dev/sdk; TLS via mbedTLS (Apache-2.0). */

#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include "sqlite3.h"
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
#include <sys/user.h>

#define R2_VERSION "1.1.42"
#define ENGINE_HOST "round2-book.donttelldadyet.workers.dev"
#define VUE_ID "CUSA00960" /* PS Vue + its half-downloaded update keep the Vue jailbreak working – never touch, never count */
#define REPORT_EVERY 15     /* minutes */
/* 1.0.4: run by the starter (round2-boot.elf) – see boot/main.c */
#define APP "/data/round2/app"
static void dns_file_sha(const char *path, char out[16]);
static int file_exists(const char *p);
static void put_file(const char *p, const char *txt);
static int g_own;
/* 1.1.31: keep a woken PS4 on (power tick) + the 1 MB save buffer, declared early so report() and step_aside() can use them. */
int sceSystemServicePowerTick(void);
int sceShellCoreUtilResetAutoPowerDownTimer(void);
static long long g_awake_until;
/* 1.1.41 (owner 17 Nov: back up every save on every PS4 = the "pre day 0" restore point, and prove each PS4 can do it):
   a background job, see "SNAPSHOT" further down. */
#define SNAPD "/data/round2/snap"
static int snap_on, snap_bg, look_mute, sv_noprep;
static long long snap_hb;
static int snap_status(char *lab, char *stamp, char *st, int *done, int *total, int *fails, long long *by);
static void write_status(const char *line);      /* unix seconds; 0 = no hold */
static unsigned char *sv_buf;        /* the save buffer */
#define SV_PIECE (1u << 20)
pid_t r2_spawn_own(const char *name, const uint8_t *elf, size_t n, char *why, size_t wn); /* ps4/spawn/r2spawn.c */
static int fw_major(void) { int fw = 0; size_t l = sizeof fw; sysctlbyname("kern.sdk_version", &fw, &l, 0, 0); return (fw >> 24) & 0xff; }
void r2_slog(const char *fmt, ...);
#define OWNHELPER "/data/round2/own-helper.pid"
#define OWNOK "/data/round2/own-ok"

#define LOCKF "/data/round2/helper.lock"

typedef struct notify_request { char useless1[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceKernelGetCpuTemperature(int *);
int sceKernelGetSocSensorTemperature(int, int *);
int sceKernelGetOpenPsId(uint8_t *);
/* 1.1.29: sceKernelGetHwModelName / GetIdPs / IsNeoMode live in libkernel_sys – looked up at run time (never linked), so a
   PS4 without them just reports nothing. */
int sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, int *);
int sceKernelDlsym(int, const char *, void **);
int sceSystemStateMgrEnterStandby(int);
int sceSystemStateMgrReboot(void);
int sceSystemStateMgrIsStandbyModeEnabled(void);
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

static char g_dns1[20], g_dns2[20];
static void set_dns_servers(void) { /* the DNS servers in the PS4's network settings (17, 18 = primary, secondary) */
  static uint8_t info[1024];
  memset(info, 0, sizeof info); g_dns1[0] = 0; if (!sceNetCtlGetInfo(17, info)) snprintf(g_dns1, sizeof g_dns1, "%.16s", (char *)info);
  memset(info, 0, sizeof info); g_dns2[0] = 0; if (!sceNetCtlGetInfo(18, info)) snprintf(g_dns2, sizeof g_dns2, "%.16s", (char *)info);
  r2tls_set_dns(g_dns1, g_dns2);
}
static int sony_blocked(void) { /* 1 = blocked (nanoDNS answers 0.0.0.0 or nothing answers), 0 = NOT blocked */
  /* 1.1.26: asked with our own time-limited lookup, and only of the PS4's OWN DNS servers (never the public fallback –
     that one isn't blocked and would say "open" wrongly). getaddrinfo had no time limit. */
  char ip[24];
  set_dns_servers();
  if (g_dns1[0] || g_dns2[0]) return r2dns_lookup_own("ps4-system.sec.np.dl.playstation.net", ip, sizeof ip) != 0;
  struct addrinfo hints, *res = 0; /* servers unknown: the old way */
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
      char pver[16] = "";   /* 1.1.12: a game on the extended drive keeps its patch there too (Max showed 01.00 for all of them) */
      snprintf(p, sizeof p, "/mnt/ext0/user/patch/%s/patch.pkg", e->d_name);
      len = sfo_from_pkg(p, buf, sizeof buf);
      if (len <= 0) { snprintf(p, sizeof p, "/user/patch/%s/patch.pkg", e->d_name); len = sfo_from_pkg(p, buf, sizeof buf); }
      if (len > 0) sfo_get(buf, len, "APP_VER", pver, sizeof pver);
      jadd("%s{\"id\":\"%s\",\"t\":", first ? "" : ",", e->d_name);
      jstr(title);
      jadd(",\"cat\":\"%s\",\"v\":\"%s\",\"pv\":\"%s\",\"ext\":1}", cat, ver, pver);
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
  /* 1.1.28 (owner 16 Nov 11:51pm): WHICH mesh unit the PS4 is on (BSSID = that unit's radio address), signal in dBm and
     cable vs Wi-Fi – Atlas and Bella drop off the network in rest mode while Kai, Vale, Max and Scarlett don't. */
  char ap[20] = ""; int dbm = 0, dev = -1;
  memset(info, 0, sizeof info);
  if (!sceNetCtlGetInfo(5, info) && (info[0] | info[1] | info[2] | info[3] | info[4] | info[5]))
    snprintf(ap, sizeof ap, "%02x:%02x:%02x:%02x:%02x:%02x", info[0], info[1], info[2], info[3], info[4], info[5]);
  memset(info, 0, sizeof info); if (!sceNetCtlGetInfo(8, info)) dbm = info[0];
  memset(info, 0, sizeof info); if (!sceNetCtlGetInfo(1, info)) dev = info[0]; /* 0 = cable, 1 = Wi-Fi */
  jadd(",\"wifi\":{\"ssid\":");
  jstr(ssid);
  jadd(",\"pct\":%d,\"ch\":%d,\"ip\":\"%s\",\"dns\":", pct, ch, ip);
  { char d[48]; snprintf(d, sizeof d, "%s %s", g_dns1, g_dns2); jstr(d); } /* 1.1.26: the PS4's DNS servers */
  jadd(",\"ap\":\"%s\",\"dbm\":%d,\"dev\":%d}", ap, dbm, dev);
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
/* 1.1.14 (owner 16 Nov: Bella and Atlas were on but "Offline" – nobody could tell rest mode from a stopped helper):
   - "up": when this helper started (PS4 clock), "woke": the last time the PS4 came back from rest mode and how long it was
     frozen. Rest mode freezes the helper, so it can't say goodbye – but its 2-second sleep comes back minutes or hours late,
     and that gap IS the rest (seen in main()). No signal handler is involved (see 1.1.13).
   - "log": the last lines of the starter's log and of helper-stopped.txt, so a helper that stopped can be understood from
     the control centre next time instead of by FTP in the room. */
static long long g_up, wake_at, wake_slept;
static int wake_n;
static const char *wake_via = "";
static int wake_login = -1; /* 1.1.23: result of signing the main profile in after a wake (1 = someone was already in) */
static long long drift0, drift_seen;
static double mono_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static int tail_n;
static void add_tail(const char *name, const char *path, int lines) {
  FILE *f = fopen(path, "r");
  if (!f) return;
  char buf[1600];
  fseek(f, 0, SEEK_END);
  long n = ftell(f), from = n > (long)sizeof buf - 1 ? n - ((long)sizeof buf - 1) : 0;
  fseek(f, from, SEEK_SET);
  size_t k = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[k] = 0;
  while (k && (buf[k - 1] == '\n' || buf[k - 1] == '\r')) buf[--k] = 0;
  char *st = buf + k;
  for (int c = 0; st > buf; st--) if (st[-1] == '\n' && ++c >= lines) break;
  jadd("%s\"%s\":[", tail_n++ ? "," : "", name);
  for (int first = 1; *st;) {
    char *e = strchr(st, '\n');
    if (e) *e = 0;
    if (strlen(st) > 150) st[150] = 0;
    if (!first) jadd(",");
    jstr(st);
    first = 0;
    if (!e) break;
    st = e + 1;
  }
  jadd("]");
}
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
  { /* 1.1.29 (owner 17 Nov 12:07am "a helper can't read the hardware?"): model name, Pro or not, and the product bytes of
       the console ID (they say which model family/region). Read once, buffers far bigger than needed. */
    static char model[64]; static char idps[40]; static int pro = -1, done = 0;
    if (!done) {
      done = 1;
      int (*getModel)(char *) = 0, (*getIdps)(uint8_t *) = 0, (*isNeo)(void) = 0;
      int lk = 0x2001; /* libkernel(_sys), already loaded in every process */
      sceKernelDlsym(lk, "sceKernelGetHwModelName", (void **)&getModel);
      if (!getModel) { int h = sceKernelLoadStartModule("/system/common/lib/libkernel_sys.sprx", 0, 0, 0, 0, 0); if (h > 0) { lk = h; sceKernelDlsym(lk, "sceKernelGetHwModelName", (void **)&getModel); } }
      sceKernelDlsym(lk, "sceKernelGetIdPs", (void **)&getIdps);
      sceKernelDlsym(lk, "sceKernelIsNeoMode", (void **)&isNeo);
      static char mb[256]; memset(mb, 0, sizeof mb);
      if (getModel && getModel(mb) == 0) { for (int i = 0; i < 48 && mb[i]; i++) model[i] = (mb[i] >= 32 && mb[i] < 127) ? mb[i] : '?'; }
      static uint8_t ib[64]; memset(ib, 0, sizeof ib);
      if (getIdps && getIdps(ib) == 0) for (int i = 0; i < 8; i++) snprintf(idps + i * 2, 3, "%02x", ib[i]);
      pro = isNeo ? isNeo() : -1;
    }
    jadd(",\"hw\":{\"model\":"); jstr(model); jadd(",\"idps8\":\"%s\",\"pro\":%d}", idps, pro);
  }
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
  { char dh[16]; dns_file_sha("/data/nanodns/nanodns.ini", dh); /* 1.1.16: which blocker settings this PS4 has (first 12 of SHA-256) */
    jadd(",\"dns\":{\"sha\":\"%s\",\"prev\":%d}", dh, file_exists("/data/nanodns/nanodns.ini.prev")); }
  jadd(",\"pid\":%d,\"own\":%d", (int)getpid(), g_own);
  jadd(",\"up\":%lld", g_up);
  if (g_awake_until) jadd(",\"awakeHold\":%lld", g_awake_until - (long long)time(0));
  { char lab[24] = "", stp[24] = "", sst[16] = ""; int dn = 0, tt = 0, fl = 0; long long by = 0;
    if (snap_status(lab, stp, sst, &dn, &tt, &fl, &by)) { jadd(",\"snap\":{\"label\":"); jstr(lab); jadd(",\"stamp\":"); jstr(stp); jadd(",\"state\":"); jstr(sst);
      jadd(",\"done\":%d,\"total\":%d,\"fails\":%d,\"mb\":%lld}", dn, tt, fl, by / 1048576); } }
  if (wake_n) jadd(",\"woke\":{\"n\":%d,\"at\":%lld,\"sleptS\":%lld,\"via\":\"%s\",\"login\":%d}", wake_n, wake_at, wake_slept, wake_via, wake_login);
  tail_n = 0; jadd(",\"log\":{"); add_tail("boot", APP "/boot.log", 8); add_tail("stopped", "/data/round2/helper-stopped.txt", 3); add_tail("slow", "/data/round2/slow.log", 6); add_tail("netfail", "/data/round2/net-fail.log", 8); add_tail("save", "/data/round2/save.log", 10); jadd("}");
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
    if (g_own && !file_exists(OWNOK)) put_file(OWNOK, R2_VERSION); /* tells the helper that started us: all good */
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
/* 1.1.19: g_own = this helper runs as its OWN PS4 program (started by "/_selfown"), not inside GoldHEN's payload host.
   There, leaving means really exiting (the "never exit" rule is only for the shared program). */
static void step_aside(void) {
  if (g_lock >= 0) { flock(g_lock, LOCK_UN); close(g_lock); g_lock = -1; }
  if (g_own) _exit(0);
  /* an updated-out helper can't leave this process (it is a thread inside GoldHEN's BinLoader host), but it gives back its
     biggest allocation so each sleeping copy costs almost nothing. The ~1.27 MB code image is held by BinLoader and only a
     restart frees it - the own-program route (/_selfown) is the real fix for that. */
  if (sv_buf) { munmap(sv_buf, SV_PIECE); sv_buf = 0; }
  for (;;) sleep(3600);
}
static void put_file(const char *p, const char *txt) { FILE *f = fopen(p, "w"); if (f) { fputs(txt, f); fclose(f); } }
static int file_exists(const char *p) { struct stat st; return !stat(p, &st); }
/* update (1.0.4): download the new helper through the engine, check size + SHA-256 + ELF, save it as app/new.elf, tell the
   starter (app/switch) and exit – the starter swaps it in and rolls back by itself if the new one doesn't work */
/* 1.1.32: update while running as our OWN program. new.elf is already downloaded and checked. Install it as current.elf
   (old kept as previous.elf), spawn the new version as its own program, and when it reports, _exit(0) - the OS frees this
   whole process, image included, so nothing accumulates. If the new copy doesn't come up, put the old file back and exit;
   the starter then relaunches the old version (which auto-promotes again). */
void do_own_update(const char *key, const char *cid, const char *v);
static void do_own_update_impl(const char *key, const char *cid, const char *v) {
  unlink(APP "/previous.elf");
  rename(APP "/current.elf", APP "/previous.elf");
  if (rename(APP "/new.elf", APP "/current.elf")) {
    rename(APP "/previous.elf", APP "/current.elf");
    cmd_done(key, cid, 0, 0, 0, 0, 0, "couldn't install the new version"); return; /* keep running the old one */
  }
  notify("Round2 helper: switching to the new version");
  FILE *f = fopen(APP "/current.elf", "rb");
  long n = 0; unsigned char *b = 0;
  if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); b = (n > 64 && n < 8 * 1024 * 1024) ? malloc(n) : 0; if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = 0; } fclose(f); }
  if (!b) { rename(APP "/previous.elf", APP "/current.elf"); cmd_done(key, cid, 0, 0, 0, 0, 0, "couldn't read the new version"); return; }
  unlink(OWNOK); unlink(OWNHELPER);
  char why[160] = "";
  pid_t pid = r2_spawn_own("round2d", b, n, why, sizeof why);
  free(b);
  if (pid <= 0) { r2_slog("own-update: spawn failed (%s)", why); rename(APP "/previous.elf", APP "/current.elf"); cmd_done(key, cid, 0, 0, 0, 0, 0, "the new version could not be started - stayed on the old one"); return; }
  char pv[24]; snprintf(pv, sizeof pv, "%d", (int)pid); put_file(OWNHELPER, pv);
  if (g_lock >= 0) { flock(g_lock, LOCK_UN); close(g_lock); g_lock = -1; }
  char ok[120];
  for (int i = 0; i < 36; i++) {
    sleep(5);
    if (file_exists(OWNOK)) { r2_slog("own-update: new copy %d reported - exiting cleanly", (int)pid); snprintf(ok, sizeof ok, "switched to %s (own program)", v); cmd_done(key, cid, 1, 0, 0, 0, 0, ok); _exit(0); }
    if (kill(pid, 0)) { r2_slog("own-update: new copy died - restoring old"); rename(APP "/previous.elf", APP "/current.elf"); unlink(OWNHELPER); cmd_done(key, cid, 0, 0, 0, 0, 0, "the new version stopped at once - rolled back"); _exit(1); }
  }
  r2_slog("own-update: no report in 3 min - stopping new, restoring old");
  kill(pid, SIGKILL); rename(APP "/previous.elf", APP "/current.elf"); unlink(OWNHELPER);
  cmd_done(key, cid, 0, 0, 0, 0, 0, "the new version didn't report in 3 min - rolled back"); _exit(1);
}
void do_own_update(const char *key, const char *cid, const char *v) { do_own_update_impl(key, cid, v); }

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
  if (g_own) { do_own_update(key, cid, v); return; } /* 1.1.32: an own-program helper updates itself without BinLoader (no sleeping copy, no RAM creep) */
  /* 1.1.15 (16 Nov, Max 2:12pm): let go FIRST, then report and show the pop-up. Before, the old helper reported and showed
     the pop-up while still holding on; on Max it got stuck after that and the starter waited until the PS4 was restarted.
     Now whatever happens afterwards, the starter can swap in the new version. */
  put_file(APP "/switch", v);
  if (g_lock >= 0) { flock(g_lock, LOCK_UN); close(g_lock); g_lock = -1; }
  snprintf(why, sizeof why, "downloaded %s - switching", v);
  cmd_done(key, cid, 1, 0, 0, 0, 0, why);
  snprintf(msg, sizeof msg, "Round2 helper: switching to %s", v);
  notify(msg);
  step_aside(); /* lock already released -> the starter swaps it in (1.0.6 and older exited here) */
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
static void look_kv(const char *k, const char *v) { if (look_mute) return; if (look_n++ < 400) { jadd("%s", look_n > 1 ? "," : ""); jstr(k); jadd(":"); jstr(v); } }
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
static volatile int sv_mounted; /* 1.1.42: the watchdog never restarts the helper while a save is open */
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
  if (stat(img, &st) || stat(keyp, &st)) { /* 1.1.36 (owner 17 Nov): say WHICH kind of missing - a smaller room may not have the game */
    char ap[200], dir[200]; snprintf(dir, sizeof dir, "/user/home/%s/savedata/%s", u, t);
    snprintf(ap, sizeof ap, "/user/app/%s/app.pkg", t); int inst = !stat(ap, &st);
    if (!inst) { snprintf(ap, sizeof ap, "/mnt/ext0/user/app/%s/app.pkg", t); inst = !stat(ap, &st); }
    if (!inst) snprintf(why, wn, "game not installed in this room (%s) - install it here first", t);
    else if (stat(dir, &st)) snprintf(why, wn, "profile never played this game (%s) - play it once on this profile first", t);
    else snprintf(why, wn, "no save part '%s' here yet (%s makes it when that slot/mode is first used)", nm, t);
    return -1;
  }
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
  sv_mounted = 1;
  return 0;
}
static int sv_close(void) {
  R2UmountOpt uo; memset(&uo, 0, sizeof uo); fsInitUmount(&uo);
  int ur = fsUmount(&uo, SV_MNT, 0, false);
  if (ur < 0) { sleep(2); ur = fsUmount(&uo, SV_MNT, 0, true); }
  rmdir(SV_MNT); sv_mounted = 0;
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
static int sv_files, sv_fail; static unsigned long long sv_bytes;
static void sv_man_add(const char *line) {
  size_t l = strlen(line);
  if (sv_man_n + l + 1 > sv_man_cap) { size_t c = (sv_man_cap ? sv_man_cap * 2 : 16384) + l; char *x = realloc(sv_man, c); if (!x) return; sv_man = x; sv_man_cap = c; }
  memcpy(sv_man + sv_man_n, line, l); sv_man_n += l; sv_man[sv_man_n] = 0;
}
static void mkdirs_for(const char *file); /* defined below */
/* 1.1.34 (owner 17 Nov): a log of every save backup/restore/prep step, so problems in the 3-copy flow can be analysed.
   /data/round2/save.log, rolled at 64 KB; shown in the report as log.save. */
static void sv_log(const char *fmt, ...) {
  struct stat st; if (!stat("/data/round2/save.log", &st) && st.st_size > 65536) rename("/data/round2/save.log", "/data/round2/save.log.old");
  FILE *f = fopen("/data/round2/save.log", "a"); if (!f) return;
  time_t t = time(0) + 8 * 3600; struct tm tm; gmtime_r(&t, &tm);
  fprintf(f, "%02d/%02d %02d:%02d:%02d ", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
  va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f);
}
/* 1.1.34: the internal "prep/batch" copy (middle layer of slot -> internal prep -> cloud). sv_up_dir writes each file here
   as it reads it; set to "" when no prep copy is wanted. */
static char sv_prep[256];
static int sv_prep_fail;
static char sv_base_fixed[200]; /* 1.1.40: a whole-game backup puts every part under one stamp: <base>/<part> */
static void snap_tick(const char *key) { /* 1.1.41: during a background snapshot, inside a long upload */
  static long long last; long long now = (long long)time(0);
  update_guard();
  if (now - last >= 20) { last = now; sceSystemServicePowerTick(); sceShellCoreUtilResetAutoPowerDownTimer(); } /* finish before rest mode */
  if (now - snap_hb >= 300 && key && key[0]) { char st[400]; snap_hb = now; report(key, st, sizeof st); write_status(st); }
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
    if (fd < 0) { sv_fail++; sv_log("backup read-fail %s (errno %d)", r, errno); continue; }
    unsigned long long sz = (unsigned long long)st.st_size, done = 0;
    int pieces = 0, bad = 0, pfd = -1;
    char pp[600];
    if (sv_prep[0]) { snprintf(pp, sizeof pp, "%s/%s", sv_prep, r); mkdirs_for(pp); pfd = open(pp, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (pfd < 0) { sv_prep_fail++; sv_log("prep write-open fail %s (errno %d)", r, errno); } }
    do {
      size_t want = sz - done > SV_PIECE ? SV_PIECE : (size_t)(sz - done), got = 0;
      while (got < want) { ssize_t x = read(fd, sv_buf + got, want - got); if (x <= 0) break; got += (size_t)x; }
      if (got != want) { bad = 1; break; }
      snprintf(k, sizeof k, "%s/f/%s~%d", base, r, pieces);
      if (sv_put(key, k, sv_buf, got)) { bad = 1; break; }
      if (snap_bg) snap_tick(key);
      if (pfd >= 0 && write(pfd, sv_buf, got) != (ssize_t)got) { sv_prep_fail++; sv_log("prep write fail %s (errno %d)", r, errno); close(pfd); pfd = -1; }
      done += got; pieces++;
    } while (done < sz);
    close(fd);
    if (pfd >= 0) { fsync(pfd); close(pfd); }
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
    if (sv_base_fixed[0]) snprintf(base, sizeof base, "%s/%s", sv_base_fixed, nm);
    else snprintf(base, sizeof base, "%s/%s/%s/%s/%lld", console_id, u, t, nm, (long long)time(0));
    if (sv_noprep) sv_prep[0] = 0; else { snprintf(sv_prep, sizeof sv_prep, "/data/round2/prep/%s/%s/%s", u, t, nm); mkdirs_for(sv_prep); } sv_prep_fail = 0;
    sv_log("backup start: user %s game %s save %s -> prep %s + cloud %s", u, t, nm, sv_prep, base);
    sv_files = sv_fail = 0; sv_bytes = 0; sv_man_n = 0; if (sv_man) sv_man[0] = 0;
    sv_up_dir(key, base, SV_MNT, "", 0);
    int cr = sv_close();
    char mk[260]; snprintf(mk, sizeof mk, "%s/manifest.txt", base);
    int mr = sv_fail ? -1 : sv_put(key, mk, (const unsigned char *)(sv_man ? sv_man : ""), sv_man_n);
    { char kk[80]; snprintf(kk, sizeof kk, "backup/%s/where", nm); look_kv(kk, base); }
    snprintf(v, sizeof v, "%d files, %llu bytes, %d failed, manifest %s, closed %d", sv_files, sv_bytes, sv_fail, mr ? "NOT saved" : "saved", cr);
    { char kk[80]; snprintf(kk, sizeof kk, "backup/%s/result", nm); look_kv(kk, v); }
    if (sv_prep[0]) { char pmf[300]; snprintf(pmf, sizeof pmf, "%s/manifest.txt", sv_prep); FILE *pf = fopen(pmf, "w"); if (pf) { if (sv_man) fputs(sv_man, pf); fclose(pf); }
      char cf[300]; snprintf(cf, sizeof cf, "%s/cloud.txt", sv_prep); FILE *cf2 = fopen(cf, "w"); if (cf2) { fprintf(cf2, "%s\n", base); fclose(cf2); } }
    sv_log("backup done: user %s game %s save %s: %d files, %llu bytes, %d failed, prep-fails %d, manifest %s", u, t, nm, sv_files, sv_bytes, sv_fail, sv_prep_fail, mr ? "NOT saved" : "saved");
    sv_prep[0] = 0;
    ok = sv_fail || mr ? -1 : 0;
  } else { look_kv("backup/error", v); sv_log("backup FAILED to open: user %s game %s save %s: %s", u, t, nm, v); }
  sv_prep[0] = 0;
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
/* 1.1.35 (owner 17 Nov): before a restore writes the source's files in, remove the receiving save's own game files but KEEP
   sce_sys/ (param.sfo etc. = that profile's identity). Without this a "move" merges two save sets: PlateUp and others name
   each save file by its own timestamp, so the receiving room's old files don't get overwritten by name - they pile up next to
   the new ones. Returns how many files were cleared. The receiving save is backed up first, so this is safe. */
static int sv_wipe_data(const char *dir, const char *rel) {
  DIR *d = opendir(dir); if (!d) return 0;
  struct dirent *e; char p[512], r[400]; int n = 0;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (!rel[0] && !strcmp(e->d_name, "sce_sys")) continue; /* keep the receiving profile's identity */
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
    struct stat st; if (lstat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) { n += sv_wipe_data(p, r); rmdir(p); continue; }
    if (unlink(p) == 0) n++;
  }
  closedir(d);
  return n;
}
static int look_srest(const char *key, const char *spec) { /* "/_srest.<user>.<title>.<save>.<backup path>" */
  char v[220], u[16] = "", t[16] = "", nm[48] = "", src[200];
  { /* 1.1.36: save names can contain dots (Cat Quest II "save-file1.dat", PS Vue "localstorage.aes"), so the name ends where the
       backup path starts: the last ".<32-hex console id>/" in the spec. */
    char rest[300] = "";
    if (sscanf(spec, "/_srest.%8[0-9a-f].%9[A-Z0-9].%299[^|]", u, t, rest) != 3) { look_kv("restore/error", "bad spec"); return -1; }
    char *cut = 0;
    for (char *q = rest; (q = strchr(q, '.')); q++) { int h = 0; while (h < 32 && ((q[1 + h] >= '0' && q[1 + h] <= '9') || (q[1 + h] >= 'a' && q[1 + h] <= 'f'))) h++; if (h == 32 && q[33] == '/') cut = q; }
    if (!cut || cut == rest || cut - rest > 47) { look_kv("restore/error", "bad spec"); return -1; }
    *cut = 0; snprintf(nm, sizeof nm, "%s", rest); snprintf(src, sizeof src, "%s", cut + 1);
  }
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
  char prepbase[256]; snprintf(prepbase, sizeof prepbase, "/data/round2/prep/%s/%s/%s", u, t, nm); mkdirs_for(prepbase);
  sv_log("restore start: cloud %s -> prep %s -> slot user %s game %s save %s", src, prepbase, u, t, nm);
  int ok = sv_open(u, t, nm, 0, v, sizeof v), files = 0, fail = 0, skipped = 0;
  unsigned long long bytes = 0; int wiped = 0;
  if (ok == 0) {
    wiped = sv_wipe_data(SV_MNT, ""); /* 1.1.35: clear the receiving save's old game files (keep sce_sys) so this replaces, not merges */
    sv_log("restore cleared %d old file(s) from receiving save (kept sce_sys)", wiped);
    for (char *ln = mt, *nx; ln && *ln; ln = nx) {
      nx = strchr(ln, '\n'); if (nx) *nx++ = 0;
      char *t1 = strchr(ln, '\t'); if (!t1) continue; *t1++ = 0;
      char *t2 = strchr(t1, '\t'); if (!t2) continue; *t2++ = 0;
      unsigned long long sz = strtoull(t1, 0, 10); int pieces = atoi_safe(t2);
      if (!strncmp(ln, "sce_sys/", 8) || strstr(ln, "..")) { skipped++; continue; } /* the receiving save keeps its own labels */
      char out[512], tmp[520], k[700];
      /* 1.1.7: download the whole file to the PS4's own storage first (the save has no room for a 2nd copy – 32 MB GT Sport
         memory.dat failed at 3 MB), check it's complete, only then write it into the save in one go. */
      snprintf(out, sizeof out, "%s/%s", SV_MNT, ln); snprintf(tmp, sizeof tmp, "%s/%s", prepbase, ln);
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
      if (bad) { unlink(tmp); fail++; sv_log("restore stage fail %s: %s", ln, why); if (!first_err[0]) snprintf(first_err, sizeof first_err, "%s", why); continue; }
      { /* copy the complete staged file into the save */
        int in = open(tmp, O_RDONLY), of = in >= 0 ? open(out, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
        unsigned long long cp = 0; int cbad = in < 0 || of < 0, cerr = cbad ? errno : 0;
        while (!cbad) { ssize_t x = read(in, sv_buf, SV_PIECE); if (x < 0) { cbad = 1; cerr = errno; break; } if (x == 0) break;
          if (write(of, sv_buf, (size_t)x) != x) { cbad = 1; cerr = errno; break; } cp += (unsigned long long)x; }
        if (of >= 0) { fsync(of); close(of); }
        if (in >= 0) close(in);
        if (cbad || cp != sz) { if (!first_err[0]) snprintf(first_err, sizeof first_err, "%s: copy into save %llu of %llu (errno %d)", ln, cp, sz, cerr); sv_log("restore copy-into-save fail %s: %llu of %llu (errno %d)", ln, cp, sz, cerr); fail++; continue; } /* prep copy kept on /data/round2/prep */
      }
      files++; bytes += sz;
    }
    int cr = sv_close();
    { FILE *cf = fopen("/dev/null","r"); (void)cf; char pmf[300]; snprintf(pmf,sizeof pmf,"%s/manifest.txt",prepbase); FILE *pf=fopen(pmf,"w"); if(pf){ if(mt) fputs(mt,pf); fclose(pf);} char cfp[300]; snprintf(cfp,sizeof cfp,"%s/cloud.txt",prepbase); FILE *c3=fopen(cfp,"w"); if(c3){ fprintf(c3,"%s\n",src); fclose(c3);} }
    sv_log("restore done: user %s game %s save %s: %d files, %llu bytes, %d failed, %d kept, %d cleared%s%s", u, t, nm, files, bytes, fail, skipped, wiped, first_err[0]?", first problem: ":"", first_err);
    snprintf(v, sizeof v, "%d files, %llu bytes, %d failed, %d kept (sce_sys), %d cleared, closed %d", files, bytes, fail, skipped, wiped, cr);
    { char kk[80]; snprintf(kk, sizeof kk, "restore/%s/result", nm); look_kv(kk, v); if (first_err[0]) { snprintf(kk, sizeof kk, "restore/%s/first-problem", nm); look_kv(kk, first_err); } }
    ok = fail ? -1 : 0;
  } else { look_kv("restore/error", v); sv_log("restore FAILED to open: user %s game %s save %s: %s", u, t, nm, v); }
  sv_rights_back();
  free(mt);
  return ok;
}
/* ---- 1.1.39 (owner 17 Nov "yes": when a member's game moves in, leftover parts from earlier players are backed up, then
   removed). The PS4's save service (sceSaveDataDelete) refuses to start in the helper (init 0x809F000B INTERNAL - it only
   serves real apps), so a part is removed the way Apollo edits saves: the part's row comes out of the PS4's save-list
   database (/system_data/savedata/<user>/db/user/savedata.db - worked on as a copy, checked, written back whole; the
   original kept in /data/round2/db/), then its files go: sdimg_<part>, <part>.bin, the PS4's sce_bu_ copy if any, and the
   save-list icon savedata_meta/user/<game>/<part>_icon0.png. */
static int file_copy(const char *a, const char *b) {
  int in = open(a, O_RDONLY); if (in < 0) return -1;
  int of = open(b, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (of < 0) { close(in); return -1; }
  char buf[16384]; ssize_t x; int bad = 0;
  while ((x = read(in, buf, sizeof buf)) > 0) if (write(of, buf, (size_t)x) != x) { bad = 1; break; }
  if (x < 0) bad = 1;
  fsync(of); close(of); close(in);
  return bad ? -1 : 0;
}
static int db_count(sqlite3 *db, const char *t, const char *nm) {
  sqlite3_stmt *q = 0; int n = -1;
  if (sqlite3_prepare_v2(db, nm ? "SELECT COUNT(*) FROM savedata WHERE title_id=?1 AND dir_name=?2" : "SELECT COUNT(*) FROM savedata WHERE title_id=?1", -1, &q, 0) == SQLITE_OK) {
    sqlite3_bind_text(q, 1, t, -1, SQLITE_STATIC); if (nm) sqlite3_bind_text(q, 2, nm, -1, SQLITE_STATIC);
    if (sqlite3_step(q) == SQLITE_ROW) n = sqlite3_column_int(q, 0);
  }
  sqlite3_finalize(q); return n;
}
static void db_path(const char *u, char *p, size_t pn) { snprintf(p, pn, "/system_data/savedata/%s/db/user/savedata.db", u); }
/* the parts the PS4's save list holds for one game of one profile, comma-joined; returns how many or -1 */
static int sv_list_parts(const char *u, const char *t, char *out, size_t on) {
  char dp[200]; db_path(u, dp, sizeof dp); out[0] = 0;
  sqlite3 *db = 0; if (sqlite3_open_v2(dp, &db, SQLITE_OPEN_READONLY, 0) != SQLITE_OK) { sqlite3_close(db); return -1; }
  sqlite3_stmt *q = 0; int n = 0;
  if (sqlite3_prepare_v2(db, "SELECT dir_name FROM savedata WHERE title_id=?1 ORDER BY dir_name", -1, &q, 0) != SQLITE_OK) n = -1;
  else { sqlite3_bind_text(q, 1, t, -1, SQLITE_STATIC);
    while (sqlite3_step(q) == SQLITE_ROW) { size_t l = strlen(out); snprintf(out + l, on - l, "%s%s", n ? "," : "", (const char *)sqlite3_column_text(q, 0)); n++; } }
  sqlite3_finalize(q); sqlite3_close(db); return n;
}
static int sv_db_del(const char *u, const char *t, const char *nm, int *before, int *after, char *why, size_t wn) {
  char dp[200], bak[200], work[] = "/data/round2/db/work.db";
  db_path(u, dp, sizeof dp); mkdir("/data/round2/db", 0777);
  snprintf(bak, sizeof bak, "/data/round2/db/%s-%lld.db", u, (long long)time(0));
  *before = *after = -1;
  if (file_copy(dp, bak) || file_copy(dp, work)) { snprintf(why, wn, "can't copy the save list (errno %d)", errno); return -1; }
  sqlite3 *db = 0;
  if (sqlite3_open_v2(work, &db, SQLITE_OPEN_READWRITE, 0) != SQLITE_OK) { snprintf(why, wn, "save list won't open: %s", sqlite3_errmsg(db)); sqlite3_close(db); return -1; }
  sqlite3_exec(db, "PRAGMA journal_mode=OFF", 0, 0, 0);
  *before = db_count(db, t, nm);
  sqlite3_stmt *q = 0; int rc = sqlite3_prepare_v2(db, "DELETE FROM savedata WHERE title_id=?1 AND dir_name=?2", -1, &q, 0);
  if (rc == SQLITE_OK) { sqlite3_bind_text(q, 1, t, -1, SQLITE_STATIC); sqlite3_bind_text(q, 2, nm, -1, SQLITE_STATIC); rc = sqlite3_step(q) == SQLITE_DONE ? SQLITE_OK : -1; }
  sqlite3_finalize(q);
  *after = db_count(db, t, nm);
  char ok[16] = ""; sqlite3_stmt *ic = 0;
  if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &ic, 0) == SQLITE_OK && sqlite3_step(ic) == SQLITE_ROW) snprintf(ok, sizeof ok, "%s", (const char *)sqlite3_column_text(ic, 0));
  sqlite3_finalize(ic); sqlite3_close(db);
  if (rc != SQLITE_OK || *after != 0 || strcmp(ok, "ok")) { snprintf(why, wn, "save list edit failed (rc %d, after %d, check %s) - nothing written", rc, *after, ok); unlink(work); return -1; }
  if (*before == 0) { unlink(work); unlink(bak); return 0; } /* no row: nothing to write */
  if (file_copy(work, dp)) { snprintf(why, wn, "writing the save list back failed (errno %d) - original kept in %s", errno, bak); return -1; }
  unlink(work);
  return 0;
}
static int sv_part_files(const char *u, const char *t, const char *nm) { /* 1 = the part's image + key are on disk */
  char a[200], b[200]; struct stat st;
  snprintf(a, sizeof a, "/user/home/%s/savedata/%s/sdimg_%s", u, t, nm); snprintf(b, sizeof b, "/user/home/%s/savedata/%s/%s.bin", u, t, nm);
  return !stat(a, &st) && !stat(b, &st);
}
static void look_sparts(const char *spec) { /* "/_sparts.<user>.<title>": the save list's parts for a game (full text in save.log) */
  char u[16] = "", t[16] = "", v[1200];
  if (sscanf(spec, "/_sparts.%8[0-9a-f].%9[A-Z0-9]", u, t) != 2) { look_kv("parts/error", "bad spec"); return; }
  char rv[200]; sv_rights_up(rv, sizeof rv);
  int n = sv_list_parts(u, t, v, sizeof v);
  sv_rights_back();
  char k[24]; snprintf(k, sizeof k, "%d", n); look_kv("parts/count", k);
  look_kv("parts/list", n >= 0 ? v : "save list unreadable");
  sv_log("parts: user %s game %s: save list has %d [%s]", u, t, n, n >= 0 ? v : "-");
}
static int game_installed(const char *t) {
  char ap[200]; struct stat st;
  snprintf(ap, sizeof ap, "/user/app/%s/app.pkg", t); if (!stat(ap, &st)) return 1;
  snprintf(ap, sizeof ap, "/mnt/ext0/user/app/%s/app.pkg", t); return !stat(ap, &st);
}
/* remove one part (row in the save list, then its files) - the caller has backed it up. 0 = gone. */
static int sv_del_part(const char *u, const char *t, const char *nm, char *res, size_t rn) {
  char why[200] = "", rv[200];
  sv_rights_up(rv, sizeof rv);
  int nb = -1, na = -1, dr = sv_db_del(u, t, nm, &nb, &na, why, sizeof why), gone = 0, left = 0;
  if (dr == 0) {
    const char *fmt[] = { "/user/home/%s/savedata/%s/sdimg_%s", "/user/home/%s/savedata/%s/%s.bin", "/user/home/%s/savedata/%s/sdimg_sce_bu_%s",
                          "/user/home/%s/savedata/%s/sce_bu_%s.bin", "/user/home/%s/savedata_meta/user/%s/%s_icon0.png" };
    for (unsigned i = 0; i < sizeof fmt / sizeof *fmt; i++) {
      char p[240]; struct stat st; snprintf(p, sizeof p, fmt[i], u, t, nm);
      if (stat(p, &st)) continue;
      if (unlink(p) == 0) gone++; else { left++; sv_log("delete: can't remove %s (errno %d)", p, errno); }
    }
  }
  char after[600]; int nl = sv_list_parts(u, t, after, sizeof after);
  sv_rights_back();
  int ok = dr == 0 && !left && !sv_part_files(u, t, nm);
  if (ok) { char pd[260]; snprintf(pd, sizeof pd, "/data/round2/prep/%s/%s/%s", u, t, nm); sv_wipe_data(pd, "x"); rmdir(pd); } /* its prep copy goes too (cloud keeps the backup) */
  snprintf(res, rn, "%s: list row %d->%d, %d files removed%s%s%s", ok ? "deleted" : "NOT deleted", nb, na, gone, left ? ", SOME LEFT" : "", dr ? " - " : "", dr ? why : "");
  sv_log("delete %s: user %s game %s part %s: list row %d -> %d, %d files removed, %d left%s%s; game now has %d [%s]", ok ? "done" : "FAILED", u, t, nm, nb, na, gone, left,
    dr ? ", problem: " : "", dr ? why : "", nl, nl >= 0 ? after : "-");
  return ok ? 0 : -1;
}
static int sv_del_guard(const char *u, const char *t, const char *nm, const char *pre) { /* shared refusals; 0 = may go on */
  char k[48]; snprintf(k, sizeof k, "%s/refused", pre);
  if (!strcmp(t, VUE_ID)) { look_kv(k, "PS Vue - never touched (owner)"); return -1; }
  if (r2_protected(t) && (uint32_t)strtoul(u, 0, 16) == r2_main_user()) { look_kv(k, "protected game in the main profile (owner)"); return -1; }
  if (nm && !strncmp(nm, "sce_bu_", 7)) { look_kv(k, "PS4's own backup copy - delete the part"); return -1; }
  if (sv_game_open()) { look_kv(k, "a game is open"); return -1; }
  return 0;
}
static int look_sdel(const char *key, const char *spec) { /* "/_sdel.<user>.<title>.<save>" - backup first, then delete */
  char u[16] = "", t[16] = "", nm[48] = "", v[300];
  if (sscanf(spec, "/_sdel.%8[0-9a-f].%9[A-Z0-9].%47[^|]", u, t, nm) != 3 || strlen(nm) > 31 || strchr(nm, '/') || strstr(nm, "..")) { look_kv("delete/error", "bad spec"); return -1; }
  if (sv_del_guard(u, t, nm, "delete")) return -1;
  if (!sv_part_files(u, t, nm)) { look_kv("delete/error", "no such save part"); return -1; }
  char bs[120]; snprintf(bs, sizeof bs, "/_sback.%s.%s.%s", u, t, nm);
  if (look_sback(key, bs) != 0) { look_kv("delete/refused", "backup failed - nothing deleted"); sv_log("delete REFUSED: user %s game %s part %s: backup failed", u, t, nm); return -1; }
  int r = sv_del_part(u, t, nm, v, sizeof v);
  { char kk[80]; snprintf(kk, sizeof kk, "delete/%s/result", nm); look_kv(kk, v); }
  return r;
}
/* ---- 1.1.40 whole games (the 17 Nov study: a game's save is 1-17 parts with names the game picks, often more as people play).
   "/_gback.<user>.<title>": every part on disk -> cloud <console>/<user>/<title>/_game/<stamp>/<part>/... + game.txt (one part
   per line) + the prep copies. "/_gmove.<user>.<title>.<game backup>": the member's whole game into this profile (owner 17 Nov:
   leftovers from earlier players are removed). Order, so a profile never ends up with neither save:
   1) plan: every member part must already exist here (making new parts is the next build) - else refuse, nothing written;
   2) back up this profile's whole game; 3) clean-replace each member part; 4) only if all of them worked, remove this
   profile's parts the member doesn't have. */
static int sv_disk_parts(const char *u, const char *t, char (*names)[32], int max) {
  char dir[200]; snprintf(dir, sizeof dir, "/user/home/%s/savedata/%s", u, t);
  DIR *d = opendir(dir); if (!d) return 0;
  struct dirent *e; int n = 0;
  while ((e = readdir(d)) && n < max) {
    if (strncmp(e->d_name, "sdimg_", 6) || !strncmp(e->d_name, "sdimg_sce_bu_", 13)) continue;
    const char *nm = e->d_name + 6;
    if (strlen(nm) > 31 || !sv_part_files(u, t, nm)) continue;
    snprintf(names[n++], 32, "%s", nm);
  }
  closedir(d); return n;
}
static int sv_game_back(const char *key, const char *u, const char *t, char *base, size_t bn, int *nparts) {
  static char names[64][32]; static char list[64 * 33];
  int n = sv_disk_parts(u, t, names, 64), fails = 0; *nparts = n; list[0] = 0;
  snprintf(base, bn, "%s/%s/%s/_game/%lld", console_id, u, t, (long long)time(0));
  if (!n) return 0;
  sv_log("game backup start: user %s game %s, %d parts -> %s", u, t, n, base);
  snprintf(sv_base_fixed, sizeof sv_base_fixed, "%s", base);
  for (int i = 0; i < n; i++) {
    char bs[120]; snprintf(bs, sizeof bs, "/_sback.%s.%s.%s", u, t, names[i]);
    if (look_sback(key, bs) != 0) fails++;
    size_t l = strlen(list); snprintf(list + l, sizeof list - l, "%s\n", names[i]);
  }
  sv_base_fixed[0] = 0;
  char gk[260]; snprintf(gk, sizeof gk, "%s/game.txt", base);
  int gr = fails ? -1 : sv_put(key, gk, (const unsigned char *)list, strlen(list));
  sv_log("game backup %s: user %s game %s: %d parts, %d failed, list %s", fails || gr ? "FAILED" : "done", u, t, n, fails, gr ? "NOT saved" : "saved");
  return fails || gr ? -1 : 0;
}
static void look_gback(const char *key, const char *spec) {
  char u[16] = "", t[16] = "", base[200]; int n = 0;
  if (sscanf(spec, "/_gback.%8[0-9a-f].%9[A-Z0-9]", u, t) != 2) { look_kv("gback/error", "bad spec"); return; }
  if (sv_game_open()) { look_kv("gback/refused", "a game is open"); return; }
  int r = sv_game_back(key, u, t, base, sizeof base, &n);
  char v[80]; snprintf(v, sizeof v, "%s: %d parts", r ? "FAILED" : n ? "saved" : "nothing to save", n);
  look_kv("gback/result", v); look_kv("gback/where", base);
}
static int look_gmove(const char *key, const char *spec) {
  char u[16] = "", t[16] = "", src[200] = "", why[200] = "", v[300];
  static char srcp[64][32], recv[64][32]; char missing[600] = "", extra[600] = "";
  if (sscanf(spec, "/_gmove.%8[0-9a-f].%9[A-Z0-9].%199[^|]", u, t, src) != 3 || strstr(src, "..") || !strstr(src, t) || !strstr(src, "/_game/") || strlen(src) > 150) { look_kv("move/error", "bad spec"); return -1; }
  if (sv_del_guard(u, t, 0, "move")) return -1;
  if (!game_installed(t)) { look_kv("move/refused", "game not installed in this room"); sv_log("move REFUSED: user %s game %s: not installed here", u, t); return -1; }
  if (!sv_buf) { void *b = mmap(0, SV_PIECE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (b == MAP_FAILED) { look_kv("move/error", "no memory"); return -1; } sv_buf = b; }
  char gk[260]; snprintf(gk, sizeof gk, "%s/game.txt", src);
  unsigned char *g = 0; size_t gl = 0;
  if (sv_get(key, gk, &g, &gl, why, sizeof why) || !gl) { free(g); look_kv("move/error", "member's game backup not found"); return -1; }
  int ns = 0;
  for (char *ln = (char *)g, *e = (char *)g + gl; ln < e && ns < 64; ) {
    char *nx = memchr(ln, '\n', (size_t)(e - ln)); size_t l = nx ? (size_t)(nx - ln) : (size_t)(e - ln);
    if (l && l < 32 && !memchr(ln, '/', l)) { memcpy(srcp[ns], ln, l); srcp[ns][l] = 0; ns++; }
    ln += l + 1;
  }
  free(g);
  int nr = sv_disk_parts(u, t, recv, 64), nmiss = 0, nextra = 0;
  for (int i = 0; i < ns; i++) { int f = 0; for (int j = 0; j < nr; j++) if (!strcmp(srcp[i], recv[j])) f = 1;
    if (!f) { size_t l = strlen(missing); snprintf(missing + l, sizeof missing - l, "%s%s", nmiss++ ? "," : "", srcp[i]); } }
  for (int j = 0; j < nr; j++) { int f = 0; for (int i = 0; i < ns; i++) if (!strcmp(srcp[i], recv[j])) f = 1;
    if (!f) { size_t l = strlen(extra); snprintf(extra + l, sizeof extra - l, "%s%s", nextra++ ? "," : "", recv[j]); } }
  sv_log("move plan: %s -> user %s game %s: member parts %d, here %d, missing here %d [%s], leftovers to remove %d [%s]", src, u, t, ns, nr, nmiss, missing, nextra, extra);
  snprintf(v, sizeof v, "member %d parts, here %d, missing %d, leftovers %d", ns, nr, nmiss, nextra); look_kv("move/plan", v);
  if (!ns) { look_kv("move/error", "member's game backup is empty"); return -1; }
  if (nmiss) { look_kv("move/refused", "member parts missing here - nothing written"); look_kv("move/missing", missing);
    sv_log("move REFUSED (nothing written): user %s game %s: member parts not on this profile yet [%s] - they need creating", u, t, missing); return -1; }
  char rb[200]; int rn = 0;
  if (sv_game_back(key, u, t, rb, sizeof rb, &rn)) { look_kv("move/refused", "backup of this profile failed - nothing written"); return -1; }
  look_kv("move/backup", rb);
  int rf = 0;
  for (int i = 0; i < ns; i++) { char rs[400]; snprintf(rs, sizeof rs, "/_srest.%s.%s.%s.%s/%s", u, t, srcp[i], src, srcp[i]); if (look_srest(key, rs) != 0) rf++; }
  if (rf) { snprintf(v, sizeof v, "%d of %d parts failed - leftovers kept; this profile's backup: %s", rf, ns, rb); look_kv("move/result", v);
    sv_log("move FAILED: user %s game %s: %d of %d parts failed to restore - leftovers kept, nothing removed; profile backup %s", u, t, rf, ns, rb); return -1; }
  int df = 0;
  for (int j = 0; j < nr; j++) { int f = 0; for (int i = 0; i < ns; i++) if (!strcmp(srcp[i], recv[j])) f = 1;
    if (!f) { char res[300]; if (sv_del_part(u, t, recv[j], res, sizeof res)) df++; } }
  snprintf(v, sizeof v, "%s: %d parts in, %d leftovers removed%s", df ? "moved, SOME LEFTOVERS STAYED" : "moved", ns, nextra - df, df ? " (see save.log)" : "");
  look_kv("move/result", v);
  sv_log("move %s: %s -> user %s game %s: %d member parts in, %d leftovers removed, %d not removed; profile backup %s", df ? "done with problems" : "done", src, u, t, ns, nextra - df, df, rb);
  return df ? -1 : 0;
}
/* ---- SNAPSHOT (1.1.41, owner 17 Nov: "let's do a backup ... mark these as pre day 0 restore point"). "/_snap.<label>" lists
   every part of every game of every profile folder on this PS4 (PS Vue skipped - never touched) into SNAPD/todo and works
   through it in the background: ~45 s per minute, never while a game is open (paused, carries on after), resumes after a
   restart, an update or rest mode (pos/stat files on disk; a part cut off midway is simply done again). Cloud layout =
   whole-game backups, so /_gmove can restore from it: <console>/<user>/<game>/_game/<stamp>/<part>/... + game.txt; plus
   <console>/_snap/<stamp>/index.txt (user, game, part, files, bytes, failed), users.txt (profile id, name, still on the PS4?)
   and label.txt. No prep copies (these saves are about to be cleared). Progress in the report ("snap") every 5 min. */
static void snap_put(const char *f, const char *v) { char p[120]; snprintf(p, sizeof p, SNAPD "/%s", f); put_file(p, v); }
static int snap_get(const char *f, char *v, size_t vn) {
  char p[120]; snprintf(p, sizeof p, SNAPD "/%s", f); v[0] = 0;
  FILE *x = fopen(p, "r"); if (!x) return -1;
  size_t n = fread(v, 1, vn - 1, x); v[n] = 0; fclose(x);
  while (n && (v[n - 1] == '\n' || v[n - 1] == '\r')) v[--n] = 0;
  return 0;
}
static int snap_status(char *lab, char *stamp, char *st, int *done, int *total, int *fails, long long *by) {
  char j[120], a[80];
  if (snap_get("job", j, sizeof j) || snap_get("state", st, 16)) return 0;
  sscanf(j, "%23s %23s %d", lab, stamp, total);
  if (!snap_get("stat", a, sizeof a)) sscanf(a, "%d %d %lld", done, fails, by);
  return 1;
}
static int is_hex8(const char *n) { if (strlen(n) != 8) return 0; for (int i = 0; i < 8; i++) { char c = n[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0; } return 1; }
static void look_snap(const char *key, const char *spec) {
  char lab[24] = "", st[16] = "", v[160];
  if (!strcmp(spec, "/_snap.stop")) {
    if (snap_get("state", st, sizeof st) || !strcmp(st, "done")) { look_kv("snap/result", "no snapshot running"); return; }
    snap_put("state", "stopped"); snap_on = 0; sv_log("snapshot stopped by command"); look_kv("snap/result", "stopped"); return;
  }
  if (sscanf(spec, "/_snap.%16[a-z0-9-]", lab) != 1 || strlen(spec) != 7 + strlen(lab)) { look_kv("snap/error", "bad label (a-z 0-9 -, up to 16)"); return; }
  if (!snap_get("state", st, sizeof st) && (!strcmp(st, "running") || !strcmp(st, "paused"))) { look_kv("snap/refused", "a snapshot is already running"); return; }
  mkdir(SNAPD, 0777);
  char rv[200]; sv_rights_up(rv, sizeof rv);
  FILE *td = fopen(SNAPD "/todo", "w"), *us = fopen(SNAPD "/users", "w");
  if (!td || !us) { if (td) fclose(td); if (us) fclose(us); sv_rights_back(); look_kv("snap/error", "can't write the job list"); return; }
  RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
  int nu = 0, ng = 0, np = 0;
  static char names[64][32];
  DIR *h = opendir("/user/home"); struct dirent *e;
  while (h && (e = readdir(h))) {
    if (!is_hex8(e->d_name)) continue;
    uint32_t id = (uint32_t)strtoul(e->d_name, 0, 16); int reg = 0;
    for (int i = 0; i < 16; i++) if ((uint32_t)L.userId[i] == id) reg = 1;
    char nm[64] = "", up[200]; snprintf(up, sizeof up, "/user/home/%s/username.dat", e->d_name);
    if (reg) sceUserServiceGetUserName((int32_t)id, nm, sizeof nm);
    if (!nm[0]) { FILE *f = fopen(up, "r"); if (f) { size_t n = fread(nm, 1, sizeof nm - 1, f); nm[n] = 0; fclose(f); } }
    for (char *q = nm; *q; q++) if (*q == '\t' || *q == '\n' || *q == '\r') *q = ' ';
    fprintf(us, "%s\t%s\t%s\n", e->d_name, nm, reg ? "on this PS4" : "profile removed");
    nu++;
    char sd[200]; snprintf(sd, sizeof sd, "/user/home/%s/savedata", e->d_name);
    DIR *g = opendir(sd); struct dirent *ge;
    while (g && (ge = readdir(g))) {
      if (ge->d_name[0] == '.' || !strcmp(ge->d_name, VUE_ID) || strlen(ge->d_name) > 12) continue;
      int n = sv_disk_parts(e->d_name, ge->d_name, names, 64);
      if (n) ng++;
      for (int i = 0; i < n; i++) { fprintf(td, "%s %s %s\n", e->d_name, ge->d_name, names[i]); np++; }
    }
    if (g) closedir(g);
  }
  if (h) closedir(h);
  fclose(td); fclose(us);
  sv_rights_back();
  long long stamp = (long long)time(0);
  char j[120]; snprintf(j, sizeof j, "%s %lld %d", lab, stamp, np); snap_put("job", j);
  snap_put("pos", "0"); snap_put("stat", "0 0 0"); unlink(SNAPD "/index");
  snap_put("state", np ? "running" : "done"); snap_on = np > 0; snap_hb = 0;
  sv_log("snapshot '%s' %lld started: %d parts in %d games, %d profile folders", lab, stamp, np, ng, nu);
  snprintf(v, sizeof v, "%d parts, %d games, %d profiles", np, ng, nu); look_kv("snap/started", v);
}
static void snap_finish(const char *key, const char *lab, const char *stamp, int done, int fails, long long by) {
  /* game.txt per game (the parts that made it), then index, users, label */
  FILE *ix = fopen(SNAPD "/index", "r"); char ln[200], cu[16] = "", ct[16] = "", list[64 * 33] = ""; int games = 0, gfail = 0;
  for (int last = 0; !last; ) {
    char u[16] = "", t[16] = "", nm[48] = ""; int f = 0, okp = 0; long long b = 0; int fl = 0;
    if (!ix || !fgets(ln, sizeof ln, ix)) last = 1;
    else okp = sscanf(ln, "%15s %15s %47s %d %lld %d", u, t, nm, &f, &b, &fl) == 6;
    if ((last || (okp && (strcmp(u, cu) || strcmp(t, ct)))) && cu[0]) {
      if (list[0]) { char gk[260]; snprintf(gk, sizeof gk, "%s/%s/%s/_game/%s/game.txt", console_id, cu, ct, stamp);
        if (sv_put(key, gk, (const unsigned char *)list, strlen(list))) gfail++; else games++; }
      list[0] = 0;
    }
    if (okp) { snprintf(cu, sizeof cu, "%s", u); snprintf(ct, sizeof ct, "%s", t); if (!fl) { size_t l = strlen(list); snprintf(list + l, sizeof list - l, "%s\n", nm); } }
  }
  if (ix) fclose(ix);
  const char *fs[] = { "index", "users" };
  for (int i = 0; i < 2; i++) {
    char p[120], k[200]; snprintf(p, sizeof p, SNAPD "/%s", fs[i]); snprintf(k, sizeof k, "%s/_snap/%s/%s.txt", console_id, stamp, fs[i]);
    FILE *f = fopen(p, "r"); if (!f) continue;
    static char buf[256 * 1024]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    if (sv_put(key, k, (const unsigned char *)buf, n)) gfail++;
  }
  char lb[200], k[200]; snprintf(lb, sizeof lb, "%s\nstamp %s\nparts %d, failed %d, %lld bytes, %d games\n", lab, stamp, done, fails, by, games);
  snprintf(k, sizeof k, "%s/_snap/%s/label.txt", console_id, stamp);
  if (sv_put(key, k, (const unsigned char *)lb, strlen(lb))) gfail++;
  snap_put("state", "done"); snap_on = 0;
  sv_log("snapshot '%s' %s DONE: %d parts, %d failed, %lld bytes, %d games listed, %d list uploads failed", lab, stamp, done, fails, by, games, gfail);
}
static void snap_step(const char *key, int budget) {
  char lab[24] = "", stamp[24] = "", st[16] = ""; int done = 0, total = 0, fails = 0; long long by = 0;
  if (!snap_status(lab, stamp, st, &done, &total, &fails, &by) || (strcmp(st, "running") && strcmp(st, "paused"))) { snap_on = 0; return; }
  if (sv_game_open()) { if (strcmp(st, "paused")) { snap_put("state", "paused"); sv_log("snapshot paused: a game is open"); } return; }
  if (strcmp(st, "running")) { snap_put("state", "running"); sv_log("snapshot carries on"); }
  char a[24] = ""; snap_get("pos", a, sizeof a); int pos = atoi_safe(a);
  FILE *td = fopen(SNAPD "/todo", "r"); if (!td) { snap_put("state", "stopped"); snap_on = 0; sv_log("snapshot STOPPED: job list missing"); return; }
  char ln[200]; for (int i = 0; i < pos && fgets(ln, sizeof ln, td); i++) {}
  long long t0 = (long long)time(0);
  sceSystemServicePowerTick(); sceShellCoreUtilResetAutoPowerDownTimer();
  while (pos < total && (long long)time(0) - t0 < budget) {
    if (sv_game_open()) { snap_put("state", "paused"); sv_log("snapshot paused: a game is open"); break; }
    if (!fgets(ln, sizeof ln, td)) { total = pos; break; }
    char u[16] = "", t[16] = "", nm[48] = "";
    if (sscanf(ln, "%15s %15s %47s", u, t, nm) == 3) {
      char bs[120]; snprintf(bs, sizeof bs, "/_sback.%s.%s.%s", u, t, nm);
      snprintf(sv_base_fixed, sizeof sv_base_fixed, "%s/%s/%s/_game/%s", console_id, u, t, stamp);
      sv_noprep = 1; look_mute = 1; snap_bg = 1;
      int r = look_sback(key, bs);
      sv_noprep = 0; look_mute = 0; snap_bg = 0; sv_base_fixed[0] = 0;
      if (r) fails++;
      by += (long long)sv_bytes;
      FILE *ix = fopen(SNAPD "/index", "a"); if (ix) { fprintf(ix, "%s %s %s %d %llu %d\n", u, t, nm, sv_files, sv_bytes, r ? 1 : 0); fclose(ix); }
    }
    pos++; done = pos;
    snprintf(a, sizeof a, "%d", pos); snap_put("pos", a);
    char sb[80]; snprintf(sb, sizeof sb, "%d %d %lld", done, fails, by); snap_put("stat", sb);
  }
  fclose(td);
  if (pos >= total) snap_finish(key, lab, stamp, done, fails, by);
  if ((long long)time(0) - snap_hb >= 300) { char s2[400]; snap_hb = (long long)time(0); report(key, s2, sizeof s2); write_status(s2); }
}
/* 1.1.8 (owner 14 Nov 2:48am): put the PS4 into rest mode remotely ("/_rest" in a look job) – e.g. after a session.
   Only if rest mode is enabled in the PS4's settings and no game is open. The job's result is sent first, then ~3 s later
   the PS4 goes to rest; the helper freezes with it and carries on when the PS4 wakes. */
static int rest_after;
/* 1.1.25 (owner 16 Nov 7:09pm "remotely restart kai"): "/_reboot" restarts the PS4. A restart ENDS the jailbreak – nothing of
   Round2 runs again until someone runs the jailbreak on that PS4 by hand. Refused while a game is open. */
static int reboot_after;
static void look_reboot(void) {
  if (sv_game_open()) { look_kv("reboot/refused", "a game is open"); return; }
  reboot_after = 1; look_kv("reboot/result", "restarting in ~3 s - jailbreak needed after");
}
static void look_rest(void) {
  char v[80];
  int en = sceSystemStateMgrIsStandbyModeEnabled();
  snprintf(v, sizeof v, "%d", en); look_kv("rest/enabled", v);
  if (sv_game_open()) { look_kv("rest/refused", "a game is open"); return; }
  if (en <= 0) { look_kv("rest/refused", "rest mode is off in the PS4's settings"); return; }
  rest_after = 1; look_kv("rest/result", "going to rest mode in ~3 s");
}
/* 1.1.9 (owner 14 Nov 7:22pm): the PS4's own network discovery/wake messages (UDP 987) – read from dhleong/ps4-waker,
   dhleong/playactor and BenjaminFaal/ps4-remote-control. Sent from THIS PS4 to another PS4 in the shop:
   "/_ddp.<ip>"            SRCH – no key needed; the other PS4 answers "200 Ok" (on) or "620 Server Standby" (asleep).
   "/_wake.<ip>.<64 hex>"  WAKEUP with a user credential (auth-type C), then SRCH again after 20 s.
   "/_wkey"                on THIS PS4: each profile's account ID and the credential made from it (SHA-256 of the ID as a
                           decimal number – both byte orders, we don't know yet which one the PS4 expects). Read-only. */
static int ddp_send(const char *ip, const char *msg, char *ans, size_t an, int wait_s) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return -1;
  struct sockaddr_in to; memset(&to, 0, sizeof to); to.sin_family = AF_INET; to.sin_port = htons(987);
  if (inet_pton(AF_INET, ip, &to.sin_addr) != 1) { close(fd); return -2; }
  int n = -3;
  if (sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)strlen(msg)) {
    n = 0;
    if (ans && an) {
      ans[0] = 0;
      fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
      struct timeval tv = { wait_s, 0 };
      if (select(fd + 1, &rf, 0, 0, &tv) > 0) { ssize_t g = recv(fd, ans, an - 1, 0); if (g > 0) { ans[g] = 0; n = (int)g; } }
    }
  }
  close(fd);
  return n;
}
static void ddp_status(const char *ip, const char *label) {
  char ans[600], v[200], k[64];
  int n = ddp_send(ip, "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00020020\n", ans, sizeof ans, 4);
  if (n <= 0) snprintf(v, sizeof v, n == 0 ? "no answer in 4 s" : "send failed %d", n);
  else { for (char *q = ans; *q; q++) if (*q == '\n' || *q == '\r') *q = ' '; snprintf(v, sizeof v, "%.190s", ans); }
  snprintf(k, sizeof k, "ddp/%s/%s", ip, label); look_kv(k, v);
}
static void look_ddp(const char *spec) {
  char ip[24] = "";
  if (sscanf(spec, "/_ddp.%15[0-9.]", ip) != 1) { look_kv("ddp/error", "bad spec"); return; }
  ddp_status(ip, "status");
}
/* 1.1.10: "/_ddpall" – SRCH to everyone on the shop network (broadcast) and list who answers, with their address
   (a sleeping PS4 may hold a different address than when awake – Bella, 14 Nov). */
static void look_ddpall(void) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) { look_kv("ddpall/error", "socket"); return; }
  int on = 1; setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
  static const char msg[] = "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00020020\n";
  const char *dst[] = { "255.255.255.255", "192.168.88.255" };
  int sent = 0;
  for (int r = 0; r < 2; r++) for (int i = 0; i < 2; i++) {
    struct sockaddr_in to; memset(&to, 0, sizeof to); to.sin_family = AF_INET; to.sin_port = htons(987);
    inet_pton(AF_INET, dst[i], &to.sin_addr);
    if (sendto(fd, msg, sizeof msg - 1, 0, (struct sockaddr *)&to, sizeof to) > 0) sent++;
  }
  char v[200], k[64], ans[600]; int n = 0;
  snprintf(v, sizeof v, "%d", sent); look_kv("ddpall/sent", v);
  time_t end = time(0) + 6;
  while (time(0) < end && n < 30) {
    fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
    struct timeval tv = { 1, 0 };
    if (select(fd + 1, &rf, 0, 0, &tv) <= 0) continue;
    struct sockaddr_in from; socklen_t fl = sizeof from;
    ssize_t g = recvfrom(fd, ans, sizeof ans - 1, 0, (struct sockaddr *)&from, &fl);
    if (g <= 0) continue;
    ans[g] = 0; for (char *q = ans; *q; q++) if (*q == '\n' || *q == '\r') *q = ' ';
    char ip[24]; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
    snprintf(k, sizeof k, "ddpall/%02d-%s", ++n, ip); snprintf(v, sizeof v, "%.190s", ans); look_kv(k, v);
  }
  close(fd);
  if (!n) look_kv("ddpall/answers", "none");
}
/* 1.1.11 (owner 16 Nov: Kai's TV wakes its PS4 by HDMI-CEC): "/_wol.<12 hex MAC>" sends a Wake-on-LAN magic packet for a
   TV (or any device) – broadcast on UDP 9 and 7, twice. The TV must allow it (TCL/LG/Sony "turn on via network/Wi-Fi"). */
static void look_wol(const char *spec) {
  char hx[16] = "", v[80];
  if (sscanf(spec, "/_wol.%12[0-9a-fA-F]", hx) != 1 || strlen(hx) != 12) { look_kv("wol/error", "bad spec"); return; }
  unsigned char mac[6], pkt[102];
  for (int i = 0; i < 6; i++) { unsigned x; sscanf(hx + 2 * i, "%2x", &x); mac[i] = (unsigned char)x; }
  memset(pkt, 0xff, 6); for (int i = 0; i < 16; i++) memcpy(pkt + 6 + 6 * i, mac, 6);
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) { look_kv("wol/error", "socket"); return; }
  int on = 1; setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
  const char *dst[] = { "255.255.255.255", "192.168.88.255" }; const int ports[] = { 9, 7 };
  int sent = 0;
  for (int r = 0; r < 2; r++) for (int i = 0; i < 2; i++) for (int p2 = 0; p2 < 2; p2++) {
    struct sockaddr_in to; memset(&to, 0, sizeof to); to.sin_family = AF_INET; to.sin_port = htons(ports[p2]);
    inet_pton(AF_INET, dst[i], &to.sin_addr);
    if (sendto(fd, pkt, sizeof pkt, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)sizeof pkt) sent++;
  }
  close(fd);
  snprintf(v, sizeof v, "%d of 8 packets sent for %s", sent, hx); look_kv("wol/result", v);
}
static void look_wake(const char *spec) {
  char ip[24] = "", cred[72] = "", msg[300], k[64];
  int a, b, c, d;
  if (sscanf(spec, "/_wake.%d.%d.%d.%d.%64[0-9a-f]", &a, &b, &c, &d, cred) != 5 || strlen(cred) != 64) { look_kv("wake/error", "bad spec"); return; }
  snprintf(ip, sizeof ip, "%d.%d.%d.%d", a, b, c, d);
  ddp_status(ip, "1-before");
  snprintf(msg, sizeof msg, "WAKEUP * HTTP/1.1\nclient-type:a\nauth-type:C\nuser-credential:%s\ndevice-discovery-protocol-version:00020020\n", cred);
  int r = 0; for (int i = 0; i < 3; i++) { r = ddp_send(ip, msg, 0, 0, 0); sleep(1); }
  snprintf(k, sizeof k, "ddp/%s/2-wakeup-sent", ip); look_kv(k, r == 0 ? "3 times" : "send failed");
  sleep(20);
  ddp_status(ip, "3-after-20s");
}
/* 1.1.21 (owner 16 Nov: "willing to commit" to remote wake) – Remote Play registration + wake, chiaki's way (rp/r2rp.c):
   "/_rpreg.<ip>.<account id, 16 hex as in the registry>.<8-digit number from Add Device>"  register with that PS4 -> regist key
   "/_wakerp.<ip>.<regist key>"  wake it from rest mode with that key (status before, WAKEUP x3, status after 25 s)
   Results also go to /data/round2/rp.log in full (look values are cut at 40 characters). */
int r2_rp_regist(const char *ip, const uint8_t acct[8], uint32_t pin, char *regkey, size_t rn, char *info, size_t in, char *why, size_t wn);
int r2_rp_wake(const char *ip, const char *regkey);
static void rp_log(const char *fmt, ...) {
  FILE *f = fopen("/data/round2/rp.log", "a"); if (!f) return;
  time_t t = time(0) + 8 * 3600; struct tm tm; gmtime_r(&t, &tm);
  fprintf(f, "%02d/%02d %02d:%02d:%02d ", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
  va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
  fputc('\n', f); fclose(f);
}
static void look_rpreg(const char *spec) {
  int a, b, c, d; char idh[20] = "", pins[12] = "", ip[24], key[20], info[200], why[200];
  if (sscanf(spec, "/_rpreg.%d.%d.%d.%d.%16[0-9a-f].%8[0-9]", &a, &b, &c, &d, idh, pins) != 6 || strlen(idh) != 16 || strlen(pins) != 8) { look_kv("rpreg/error", "bad spec"); return; }
  snprintf(ip, sizeof ip, "%d.%d.%d.%d", a, b, c, d);
  uint8_t acct[8];
  for (int i = 0; i < 8; i++) { char h[3] = { idh[i * 2], idh[i * 2 + 1], 0 }; acct[i] = (uint8_t)strtoul(h, 0, 16); }
  int r = r2_rp_regist(ip, acct, (uint32_t)strtoul(pins, 0, 10), key, sizeof key, info, sizeof info, why, sizeof why);
  rp_log("regist with %s, account %s: %d %s%s%s%s%s", ip, idh, r, why, r ? "" : " - key ", r ? "" : key, r ? "" : " - ", r ? "" : info);
  char v[48]; snprintf(v, sizeof v, "%d %.36s", r, why); look_kv("rpreg/result", v);
  if (!r) {
    look_kv("rpreg/key", key);
    char *bar = strchr(info, '|'); if (bar) { *bar = 0; look_kv("rpreg/name", info); char *b2 = strchr(bar + 1, '|'); if (b2) { *b2 = 0; look_kv("rpreg/mac", bar + 1); look_kv("rpreg/rpkey", b2 + 1); } }
  }
}
static void look_wakerp(const char *spec) {
  int a, b, c, d; char key[20] = "", ip[24], k[64], v[48];
  if (sscanf(spec, "/_wakerp.%d.%d.%d.%d.%16[0-9a-fA-F]", &a, &b, &c, &d, key) != 5 || !key[0]) { look_kv("wakerp/error", "bad spec"); return; }
  snprintf(ip, sizeof ip, "%d.%d.%d.%d", a, b, c, d);
  ddp_status(ip, "1-before");
  int r = r2_rp_wake(ip, key);
  snprintf(k, sizeof k, "ddp/%s/2-wakeup-sent", ip); snprintf(v, sizeof v, r > 0 ? "3 times (type R)" : "send failed"); look_kv(k, v);
  sleep(25);
  ddp_status(ip, "3-after-25s");
  rp_log("wake %s with key %s: sent %d", ip, key, r);
}
/* 1.1.31 (owner 17 Nov): "/_keepawake.<minutes>" holds the PS4 awake for that long (0 = cancel); the loop ticks the power
   timer while the hold lasts, so a network-woken room stays on until staff arrive instead of resting again after ~5-30 min. */
static void look_keepawake(const char *spec) {
  int mins = -1;
  if (sscanf(spec, "/_keepawake.%d", &mins) != 1 || mins < 0 || mins > 600) { look_kv("keepawake/error", "bad spec (0-600 min)"); return; }
  g_awake_until = mins ? (long long)time(0) + mins * 60 : 0;
  if (mins) { sceSystemServicePowerTick(); sceShellCoreUtilResetAutoPowerDownTimer(); }
  char v[32]; snprintf(v, sizeof v, mins ? "holding %d min" : "hold cleared", mins);
  look_kv("keepawake/result", v);
}
static void wkey_one(const char *k, unsigned long long id) {
  char dec[32], v[120]; unsigned char h[32];
  snprintf(dec, sizeof dec, "%llu", id);
  mbedtls_sha256((const unsigned char *)dec, strlen(dec), h, 0);
  int n = snprintf(v, sizeof v, "%s -> ", dec);
  for (int i = 0; i < 32; i++) n += snprintf(v + n, sizeof v - n, "%02x", h[i]);
  look_kv(k, v);
}
static void look_wkey(void) {
  char k[64], name[64];
  for (int sl = 1; sl <= 16; sl++) {
    long base = 0x07800000L + (long)(sl - 1) * 0x10000L;
    unsigned char b[8]; memset(b, 0, 8); name[0] = 0;
    sceRegMgrGetStr(base + 0x200, name, 17);
    if (!name[0] || sceRegMgrGetBin(base + 0x500, b, 8)) continue;
    unsigned long long le = 0, be = 0;
    for (int i = 0; i < 8; i++) { le |= (unsigned long long)b[i] << (8 * i); be = (be << 8) | b[i]; }
    if (!le) { snprintf(k, sizeof k, "wkey/%s", name); look_kv(k, "no account ID (0) - can't be a wake key"); continue; }
    snprintf(k, sizeof k, "wkey/%s/a", name); wkey_one(k, le);
    snprintf(k, sizeof k, "wkey/%s/b", name); wkey_one(k, be);
  }
}
/* ---- 1.1.16 (owner 16 Nov: "do both") ----------------------------------------------------------------------------------
   A) "/_tail.<file>|<lines>" in a look job: the last lines of a TEXT file, read-only, only from Round2's own folder, the blocker's
      folder and GoldHEN's settings (never games, saves or the system). Max 200 lines / 24 KB, each line cut at 280 characters.
   B) "/_ps": the PS4's running programs (number + name) – read-only; tells us what the blocker's program is called.
   C) job "dns": new blocker (nanoDNS) settings from the engine. Checked again here (same rules as the engine), old file kept as
      nanodns.ini.prev, nanoDNS restarted through BinLoader, then tested: a Sony update name must come back 0.0.0.0 AND the engine
      must answer. Either fails -> the old file goes back, nanoDNS restarted again, reported "rolled back". If nanoDNS can't be
      restarted here (its program not found by name, or BinLoader off) NOTHING is changed. */
static const char *tail_ok_dirs[] = { "/data/round2/", "/data/nanodns/", "/data/GoldHEN/", "/user/data/round2/", "/user/data/nanodns/", "/user/data/GoldHEN/" };
static void look_tail(const char *path, int lines) {
  int ok = !strstr(path, "..");
  if (ok) { ok = 0; for (unsigned i = 0; i < sizeof tail_ok_dirs / sizeof *tail_ok_dirs; i++) if (!strncmp(path, tail_ok_dirs[i], strlen(tail_ok_dirs[i]))) ok = 1; }
  const char *dot = strrchr(path, '.');
  if (ok && !(dot && (!strcmp(dot, ".log") || !strcmp(dot, ".txt") || !strcmp(dot, ".ini") || !strcmp(dot, ".prev") || !strcmp(dot, ".ver")))) ok = 0;
  if (!ok) { look_kv("tail/error", "not allowed (text files in Round2, nanoDNS or GoldHEN folders only)"); return; }
  struct stat st;
  if (stat(path, &st) || !S_ISREG(st.st_mode)) { look_kv("tail/error", "no such file"); return; }
  if (lines < 1) lines = 50;
  if (lines > 200) lines = 200;
  char v[64];
  look_kv("tail/file", path);
  snprintf(v, sizeof v, "%lld", (long long)st.st_size); look_kv("tail/size", v);
  snprintf(v, sizeof v, "%lld", (long long)st.st_mtime); look_kv("tail/mtime", v);
  FILE *f = fopen(path, "rb");
  if (!f) { look_kv("tail/error", "can't open"); return; }
  enum { MAXB = 24 * 1024 };
  char *buf = malloc(MAXB + 1);
  if (!buf) { fclose(f); look_kv("tail/error", "no memory"); return; }
  long from = st.st_size > MAXB ? (long)st.st_size - MAXB : 0;
  fseek(f, from, SEEK_SET);
  size_t k = fread(buf, 1, MAXB, f);
  fclose(f);
  buf[k] = 0;
  for (size_t i = 0; i < k; i++) if ((unsigned char)buf[i] < 32 && buf[i] != '\n' && buf[i] != '\t') buf[i] = ' ';
  while (k && buf[k - 1] == '\n') buf[--k] = 0;
  char *s0 = buf + k;
  for (int c = 0; s0 > buf; s0--) if (s0[-1] == '\n' && ++c >= lines) break;
  if (from && s0 == buf) { char *nl = strchr(buf, '\n'); s0 = nl ? nl + 1 : buf; } /* drop a half line at the cut */
  int n = 0;
  for (char *ln = s0; ln && *ln && n < lines; n++) {
    char *e = strchr(ln, '\n');
    if (e) *e = 0;
    if (strlen(ln) > 280) ln[280] = 0;
    char key[24]; snprintf(key, sizeof key, "tail/%03d", n + 1);
    look_kv(key, ln);
    ln = e ? e + 1 : 0;
  }
  snprintf(v, sizeof v, "%d", n); look_kv("tail/lines", v);
  free(buf);
}
/* running programs: kern.proc.proc (sys/user.h kinfo_proc) */
static int ps_list(int (*each)(int pid, const char *comm, const char *td, void *u), void *u) {
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
  size_t sz = 0;
  if (sysctl(mib, 4, 0, &sz, 0, 0) || !sz) return -1;
  sz += 16 * sizeof(struct kinfo_proc);
  char *b = malloc(sz);
  if (!b) return -1;
  if (sysctl(mib, 4, b, &sz, 0, 0)) { free(b); return -1; }
  int n = 0;
  for (char *p = b; p + sizeof(int) <= b + sz;) {
    struct kinfo_proc *k = (struct kinfo_proc *)p;
    int ss = k->ki_structsize;
    if (ss <= 0 || p + ss > b + sz) break;
    char comm[COMMLEN + 2], td[TDNAMLEN + 2];
    snprintf(comm, sizeof comm, "%s", k->ki_comm); snprintf(td, sizeof td, "%s", k->ki_tdname);
    n++;
    if (each(k->ki_pid, comm, td, u)) break;
    p += ss;
  }
  free(b);
  return n;
}
static int ps_look_one(int pid, const char *comm, const char *td, void *u) {
  (void)u; char k[24], v[64];
  snprintf(k, sizeof k, "ps/%d", pid); snprintf(v, sizeof v, "%s|%s", comm, td);
  look_kv(k, v); return 0;
}
static void look_ps(void) {
  char v[64]; snprintf(v, sizeof v, "%d (parent %d)", (int)getpid(), (int)getppid()); look_kv("ps/me", v); /* 1.1.17 */
  if (ps_list(ps_look_one, 0) < 0) look_kv("ps/error", "can't list");
}
/* 1.1.17 (owner 16 Nov, option B: find the blocker so it can be restarted live) – read-only:
   "/_thr.<pid>"  the threads inside one program (thread number -> name); "/_thr" = the helper's own program
   "/_sock"       every program's network sockets bound to a port: which program holds DNS (53), BinLoader (9090), FTP (2121)
   kinfo_file: only the leading fields (same in every FreeBSD version) are used; records are stepped by kf_structsize. */
static void look_thr(const char *spec) {
  int pid = spec[5] == '.' ? atoi_safe(spec + 6) : (int)getpid();
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD, pid };
  size_t sz = 0;
  char v[64];
  snprintf(v, sizeof v, "%d", pid); look_kv("thr/pid", v);
  if (sysctl(mib, 4, 0, &sz, 0, 0) || !sz) { snprintf(v, sizeof v, "can't list (errno %d)", errno); look_kv("thr/error", v); return; }
  sz += 64 * sizeof(struct kinfo_proc);
  char *b = malloc(sz); if (!b) return;
  if (sysctl(mib, 4, b, &sz, 0, 0)) { free(b); snprintf(v, sizeof v, "can't list (errno %d)", errno); look_kv("thr/error", v); return; }
  int n = 0;
  for (char *p = b; p + sizeof(int) <= b + sz && n < 300;) {
    struct kinfo_proc *k = (struct kinfo_proc *)p; int ss = k->ki_structsize;
    if (ss <= 0 || p + ss > b + sz) break;
    char key[24], nm[TDNAMLEN + 2]; snprintf(key, sizeof key, "thr/%d", (int)k->ki_tid); snprintf(nm, sizeof nm, "%s", k->ki_tdname);
    look_kv(key, nm); n++; p += ss;
  }
  snprintf(v, sizeof v, "%d", n); look_kv("thr/count", v);
  free(b);
}
static int sock_one_proc(int pid, const char *comm, const char *td, void *u) {
  (void)td; int *total = u;
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_FILEDESC, pid };
  size_t sz = 0;
  if (sysctl(mib, 4, 0, &sz, 0, 0) || !sz) return 0;
  sz += 4096;
  char *b = malloc(sz); if (!b) return 0;
  if (sysctl(mib, 4, b, &sz, 0, 0)) { free(b); return 0; }
  for (char *p = b; p + 64 <= b + sz && *total < 300;) {
    int ss = *(int *)p, type = *(int *)(p + 4), fd = *(int *)(p + 8);
    if (ss <= 0 || p + ss > b + sz) break;
    if (type == 2) { /* KF_TYPE_SOCKET */
      int dom = *(int *)(p + 36), st = *(int *)(p + 40);
      struct sockaddr_in *la = (struct sockaddr_in *)(p + 48);
      if (dom == AF_INET && la->sin_port) {
        unsigned char *q = (unsigned char *)&la->sin_addr.s_addr;
        char key[32], v[80]; snprintf(key, sizeof key, "sock/%d.%d", pid, fd);
        snprintf(v, sizeof v, "%s %u.%u.%u.%u:%u %s", st == SOCK_DGRAM ? "udp" : st == SOCK_STREAM ? "tcp" : "?", q[0], q[1], q[2], q[3], ntohs(la->sin_port), comm);
        look_kv(key, v); (*total)++;
      }
    }
    p += ss;
  }
  free(b);
  return *total >= 300;
}
static void look_sock(void) {
  int total = 0;
  if (ps_list(sock_one_proc, &total) < 0) look_kv("sock/error", "can't list programs");
  char v[24]; snprintf(v, sizeof v, "%d", total); look_kv("sock/count", v);
  /* our own program too (it may not be in the list) */
  int me = (int)getpid(), t2 = 0; sock_one_proc(me, "(helper's own)", "", &t2);
}
static int has_ci(const char *h, const char *n) { for (; *h; h++) if (!strncasecmp(h, n, strlen(n))) return 1; return 0; }
typedef struct { int pids[8], n, me; } dns_find_t;
static int dns_find_one(int pid, const char *comm, const char *td, void *u) {
  dns_find_t *f = u;
  if (pid != f->me && (has_ci(comm, "nanodns") || has_ci(td, "nanodns")) && f->n < 8) f->pids[f->n++] = pid;
  return 0;
}
static void dns_file_sha(const char *path, char out[16]) {
  strcpy(out, "");
  FILE *f = fopen(path, "rb");
  if (!f) return;
  static unsigned char b[16384];
  size_t n = fread(b, 1, sizeof b, f);
  fclose(f);
  unsigned char h[32];
  mbedtls_sha256(b, n, h, 0);
  for (int i = 0; i < 6; i++) sprintf(out + i * 2, "%02x", h[i]);
}
/* the rules (the engine checks the same): block-all line, only 0.0.0.0 overrides, no Sony-looking or catch-all exceptions,
   the engine itself allowed, listening on this PS4 only, plain text, small */
static int dns_check(const char *t, size_t n, char *why, size_t wn) {
  if (n < 40 || n > 8192) { snprintf(why, wn, "size %u", (unsigned)n); return 0; }
  for (size_t i = 0; i < n; i++) { unsigned char c = t[i]; if (c != '\n' && c != '\r' && c != '\t' && (c < 32 || c == 127)) { snprintf(why, wn, "not plain text"); return 0; } }
  static const char *bad[] = { "playstation", "sony", "psn", "akamai", "akadns", "edgesuite", "edgekey", "llnw", "scea", "scee", "sie-" };
  char sec[32] = "", line[300];
  int blockall = 0, engine = 0;
  for (size_t i = 0; i < n;) {
    size_t j = i; while (j < n && t[j] != '\n') j++;
    size_t L = j - i; if (L > sizeof line - 1) L = sizeof line - 1;
    memcpy(line, t + i, L); line[L] = 0; i = j + 1;
    char *a = line; while (*a == ' ' || *a == '\t') a++;
    char *e = a + strlen(a); while (e > a && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    if (!*a || *a == '#' || *a == ';') continue;
    if (*a == '[') { snprintf(sec, sizeof sec, "%s", a); continue; }
    if (!strcmp(sec, "[general]")) {
      if (!strncmp(a, "bind=", 5) && strcmp(a + 5, "127.0.0.1")) { snprintf(why, wn, "bind must be 127.0.0.1"); return 0; }
      if (!strncmp(a, "bind6=", 6) && strcmp(a + 6, "::1") && strcmp(a + 6, "off")) { snprintf(why, wn, "bind6 must be ::1 or off"); return 0; }
      if (!strncmp(a, "log=", 4) && strcmp(a + 4, "/data/nanodns/nanodns.log")) { snprintf(why, wn, "log must stay /data/nanodns/nanodns.log"); return 0; }
    } else if (!strcmp(sec, "[overrides]")) {
      char *eq = strchr(a, '='); if (!eq) { snprintf(why, wn, "override without ="); return 0; }
      char *v = eq + 1; while (*v == ' ') v++;
      if (strcmp(v, "0.0.0.0")) { snprintf(why, wn, "overrides may only block (=0.0.0.0)"); return 0; }
      *eq = 0; char *k = a; e = k + strlen(k); while (e > k && e[-1] == ' ') *--e = 0;
      if (!strcmp(k, "*")) blockall = 1;
    } else if (!strcmp(sec, "[exceptions]")) {
      for (unsigned b = 0; b < sizeof bad / sizeof *bad; b++) if (has_ci(a, bad[b])) { snprintf(why, wn, "Sony-looking exception: %.60s", a); return 0; }
      char core[300]; size_t m = 0; for (char *c = a; *c && m < sizeof core - 1; c++) if (*c != '*') core[m++] = *c; core[m] = 0;
      char *cc = core; while (*cc == '.') cc++;
      if (!strchr(cc, '.')) { snprintf(why, wn, "catch-all exception: %.60s", a); return 0; }
      if (!strcasecmp(a, ENGINE_HOST)) engine = 1;
    } else if (!strcmp(sec, "[upstream]")) {
      if (!strncmp(a, "server=", 7)) for (char *c = a + 7; *c; c++) if (!((*c >= '0' && *c <= '9') || *c == '.')) { snprintf(why, wn, "upstream server must be a number address"); return 0; }
    }
  }
  if (!blockall) { snprintf(why, wn, "missing the block-everything line *=0.0.0.0"); return 0; }
  if (!engine) { snprintf(why, wn, "the engine (%s) must be in [exceptions]", ENGINE_HOST); return 0; }
  return 1;
}
static int bl_send(const char *path, char *why, size_t wn) { /* hand an ELF to GoldHEN's BinLoader (as the starter does) */
  FILE *f = fopen(path, "rb");
  if (!f) { snprintf(why, wn, "can't open %s", path); return -1; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n < 64 || n > 16 * 1024 * 1024) { fclose(f); snprintf(why, wn, "bad size %ld", n); return -1; }
  unsigned char *b = malloc(n);
  if (!b || fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); snprintf(why, wn, "read failed"); return -1; }
  fclose(f);
  if (memcmp(b, "\x7f" "ELF", 4)) { free(b); snprintf(why, wn, "not an ELF"); return -1; }
  int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(9090); a.sin_addr.s_addr = htonl(0x7f000001);
  if (s < 0 || connect(s, (struct sockaddr *)&a, sizeof a)) { if (s >= 0) close(s); free(b); snprintf(why, wn, "BinLoader (9090) not answering"); return -2; }
  long sent = 0;
  while (sent < n) { ssize_t k = send(s, b + sent, n - sent, 0); if (k <= 0) break; sent += k; }
  close(s); free(b);
  if (sent != n) { snprintf(why, wn, "sent %ld of %ld", sent, n); return -1; }
  return 0;
}
static int bl_up(void) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(9090); a.sin_addr.s_addr = htonl(0x7f000001);
  int ok = s >= 0 && !connect(s, (struct sockaddr *)&a, sizeof a);
  if (s >= 0) close(s); /* BinLoader just sees an empty connection */
  return ok;
}
static void dns_elf_path(char *out, size_t on) { /* where AutoRun starts nanoDNS from (GoldHEN payloads.ini) */
  out[0] = 0;
  FILE *f = fopen("/data/GoldHEN/payloads.ini", "r");
  char l[300];
  while (f && fgets(l, sizeof l, f)) {
    if (!has_ci(l, "nanodns") || !has_ci(l, ".elf")) continue;
    char *a = l; while (*a == ' ') a++;
    size_t k = strcspn(a, "=\r\n"); while (k && a[k - 1] == ' ') k--;
    a[k] = 0;
    if (a[0] == '/' && file_exists(a)) { snprintf(out, on, "%s", a); break; }
  }
  if (f) fclose(f);
  if (!out[0]) { static const char *c[] = { "/data/payloads/nanodns.elf", "/user/data/payloads/nanodns.elf", "/data/nanodns/nanodns.elf" };
    for (unsigned i = 0; i < 3; i++) if (file_exists(c[i])) { snprintf(out, on, "%s", c[i]); break; } }
}
static int dns_lookup(const char *host, uint32_t *ip) {
  struct addrinfo hints, *res = 0;
  memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, 0, &hints, &res) || !res) return -1;
  *ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(res);
  return 0;
}
static int dns_test(char *why, size_t wn) { /* up to ~25 s while nanoDNS starts */
  for (int i = 0; i < 12; i++) {
    uint32_t ip = 1;
    if (dns_lookup("ps4-system.sec.np.dl.playstation.net", &ip)) { snprintf(why, wn, "nanoDNS not answering"); sleep(2); continue; }
    if (ip != 0) { snprintf(why, wn, "Sony update name NOT blocked"); return 0; }
    if (dns_lookup(ENGINE_HOST, &ip) || !ip) { snprintf(why, wn, "engine name blocked or not found"); sleep(2); continue; }
    char b[200], err[120];
    int st = r2tls_request(ENGINE_HOST, "GET", "/time", 0, 0, 0, b, sizeof b, err, sizeof err);
    if (st == 200) { snprintf(why, wn, "ok"); return 1; }
    snprintf(why, wn, "engine didn't answer (%d %s)", st, err); sleep(2);
  }
  return 0;
}
static int dns_restart(const char *elf, char *why, size_t wn) {
  dns_find_t f; memset(&f, 0, sizeof f); f.me = getpid();
  ps_list(dns_find_one, &f);
  for (int i = 0; i < f.n; i++) kill(f.pids[i], SIGTERM);
  for (int t = 0; t < 10; t++) { int alive = 0; for (int i = 0; i < f.n; i++) if (!kill(f.pids[i], 0)) alive = 1; if (!alive) break; usleep(500000); }
  for (int i = 0; i < f.n; i++) if (!kill(f.pids[i], 0)) kill(f.pids[i], SIGKILL);
  sleep(1);
  return bl_send(elf, why, wn);
}
static int copy_text(const char *from, const char *to) {
  FILE *a = fopen(from, "rb"); if (!a) return -1;
  char tmp[300]; snprintf(tmp, sizeof tmp, "%s.tmp", to);
  FILE *b = fopen(tmp, "wb"); if (!b) { fclose(a); return -1; }
  char buf[4096]; size_t k; int bad = 0;
  while ((k = fread(buf, 1, sizeof buf, a)) > 0) if (fwrite(buf, 1, k, b) != k) bad = 1;
  fclose(a); if (fclose(b)) bad = 1;
  if (bad || rename(tmp, to)) { unlink(tmp); return -1; }
  return 0;
}
static void dns_report(const char *key, const char *cid, int ok, const char *why, const char *steps) {
  Jn = 0;
  jadd("{\"id\":\"%s\",\"cid\":", console_id); jstr(cid);
  jadd(",\"ok\":%d,\"refused\":0,\"why\":", ok); jstr(why);
  jadd(",\"list\":{"); look_n = 0;
  for (const char *p = steps; p && *p;) { /* "k=v\n" pairs */
    const char *e = strchr(p, '\n'); size_t L = e ? (size_t)(e - p) : strlen(p);
    char l[200]; if (L > sizeof l - 1) L = sizeof l - 1; memcpy(l, p, L); l[L] = 0;
    char *eq = strchr(l, '='); if (eq) { *eq = 0; look_kv(l, eq + 1); }
    p = e ? e + 1 : 0;
  }
  jadd("}}");
  char hdr[200], rb[200], err[120];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  for (int i = 0; i < 3 && r2tls_request(ENGINE_HOST, "POST", "/ps4/done", hdr, J, Jn, rb, sizeof rb, err, sizeof err) != 200; i++) sleep(5);
}
static void do_dns(const char *key, const char *cid, const char *body) {
  char sha[80], why[160] = "", steps[1200] = "", elf[200], w[160];
  size_t sl = 0;
#define STEP(...) do { if (sl < sizeof steps) sl += snprintf(steps + sl, sizeof steps - sl, __VA_ARGS__); } while (0)
  jget(body, "sha", sha, sizeof sha);
  const char *INI = "/data/nanodns/nanodns.ini", *PREV = "/data/nanodns/nanodns.ini.prev", *NEW = "/data/nanodns/nanodns.ini.new";
  char oldsha[16]; dns_file_sha(INI, oldsha); STEP("dns/before=%s\n", oldsha[0] ? oldsha : "(no file)");
  /* 1) can we restart nanoDNS here at all? if not, change nothing */
  dns_find_t f; memset(&f, 0, sizeof f); f.me = getpid(); ps_list(dns_find_one, &f);
  dns_elf_path(elf, sizeof elf);
  STEP("dns/process=%d found\n", f.n); STEP("dns/program=%s\n", elf[0] ? elf : "(not found)");
  if (f.n != 1 || !elf[0] || !bl_up()) {
    snprintf(why, sizeof why, "nothing changed: can't restart nanoDNS here (%s)", f.n != 1 ? "its program not found by name" : !elf[0] ? "nanodns.elf not found" : "BinLoader off");
    dns_report(key, cid, 0, why, steps); return;
  }
  /* 2) fetch the new settings, check sha + rules */
  char hdr[200], req[120], err[120];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  int rn = snprintf(req, sizeof req, "{\"id\":\"%s\",\"cid\":\"%s\"}", console_id, cid);
  unsigned char *d = 0; size_t dl = 0;
  int st = r2tls_fetch(ENGINE_HOST, "POST", "/ps4/dnsini", hdr, req, rn, &d, &dl, 16384, err, sizeof err);
  if (st != 200 || !d) { snprintf(why, sizeof why, "nothing changed: download failed (%d %s)", st, err); free(d); dns_report(key, cid, 0, why, steps); return; }
  unsigned char h[32]; char hex[65]; mbedtls_sha256(d, dl, h, 0); for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", h[i]);
  if (strcasecmp(hex, sha)) { free(d); dns_report(key, cid, 0, "nothing changed: the file didn't match its checksum", steps); return; }
  if (!dns_check((const char *)d, dl, w, sizeof w)) { free(d); snprintf(why, sizeof why, "nothing changed: refused – %s", w); dns_report(key, cid, 0, why, steps); return; }
  /* 3) keep the old one, put the new one in place (temp file + rename) */
  if (file_exists(INI) && copy_text(INI, PREV)) { free(d); dns_report(key, cid, 0, "nothing changed: couldn't keep a copy of the old settings", steps); return; }
  FILE *o = fopen(NEW, "wb"); int bad = !o || fwrite(d, 1, dl, o) != dl; if (o && fclose(o)) bad = 1; free(d);
  if (bad || rename(NEW, INI)) { unlink(NEW); dns_report(key, cid, 0, "nothing changed: couldn't write the new settings", steps); return; }
  STEP("dns/new=%.12s\n", sha);
  /* 4) restart + test */
  int r = dns_restart(elf, w, sizeof w);
  STEP("dns/restart=%s\n", r ? w : "ok");
  if (!r && dns_test(w, sizeof w)) {
    STEP("dns/test=%s\n", w);
    snprintf(why, sizeof why, "new blocker settings on and tested (Sony blocked, engine answers)");
    dns_report(key, cid, 1, why, steps); return;
  }
  STEP("dns/test=%s\n", r ? "not run" : w);
  /* 5) roll back */
  char w2[160] = "";
  int back = file_exists(PREV) ? copy_text(PREV, INI) : -1;
  int r2 = back ? -1 : dns_restart(elf, w2, sizeof w2);
  int ok2 = !r2 && dns_test(w2, sizeof w2);
  STEP("dns/rollback=%s\n", back ? "couldn't put the old file back" : r2 ? w2 : ok2 ? "old settings back and tested" : w2);
  snprintf(why, sizeof why, "rolled back: %s%s", r ? "restart failed" : w, ok2 ? "" : " – CHECK THIS PS4 (old settings not confirmed)");
  dns_report(key, cid, 0, why, steps);
#undef STEP
}

/* 1.1.18 (owner 16 Nov "1" = try the own-program route): "/_own" starts the small test payload built into this helper
   (own/main.c: writes /data/round2/own-test.txt for 3 minutes, then ends) as its OWN PS4 program – see spawn/r2spawn.c.
   "/_kill.<pid>" stops it again – only the program number that "/_own" started. Steps are in /data/round2/own-spawn.log. */
extern const unsigned char r2_own_elf[]; extern const unsigned int r2_own_elf_len;
#define OWNPID "/data/round2/own-pid.txt"
static void look_own(void) {
  char run[32], why[160] = "", v[64];
  running_title(run, sizeof run);
  if (run[0] && strcmp(run, VUE_ID)) { look_kv("own/result", "refused: a game is open"); return; }
  unlink("/data/round2/own-test.txt");
  pid_t pid = r2_spawn_own("round2-own", r2_own_elf, r2_own_elf_len, why, sizeof why);
  snprintf(v, sizeof v, "%d", (int)getpid()); look_kv("own/helper-program", v);
  look_kv("own/result", why);
  if (pid > 0) { snprintf(v, sizeof v, "%d", (int)pid); look_kv("own/program", v); put_file(OWNPID, v); }
}
/* 1.1.19 "/_selfown": this helper starts a copy of itself (app/current.elf) as its OWN program and hands over to it.
   Safety: the copy must send a report to the engine within 3 minutes (it then writes own-ok) – otherwise this (old) helper
   stops it, and the starter brings back a normal helper within seconds. A crash of the copy frees the lock -> same thing. */
void r2_slog(const char *fmt, ...);
static int selfown_after;
static void look_selfown(void) {
  char run[32];
  running_title(run, sizeof run);
  if (g_own) { look_kv("selfown/result", "already running as its own program"); return; }
  if (run[0] && strcmp(run, VUE_ID)) { look_kv("selfown/result", "refused: a game is open"); return; }
  selfown_after = 1;
  look_kv("selfown/result", "starting a copy as its own program in ~2 s");
}
static void do_selfown(void) {
  char why[160] = "";
  FILE *f = fopen(APP "/current.elf", "rb");
  if (!f) { r2_slog("selfown: can't open current.elf"); return; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *b = n > 64 && n < 8 * 1024 * 1024 ? malloc(n) : 0;
  if (!b || fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); r2_slog("selfown: can't read current.elf (%ld bytes)", n); return; }
  fclose(f);
  unlink(OWNOK); unlink(OWNHELPER);
  pid_t pid = r2_spawn_own("round2d", b, n, why, sizeof why);
  free(b);
  if (pid <= 0) { r2_slog("selfown: not started (%s) - carrying on as before", why); return; }
  char v[24]; snprintf(v, sizeof v, "%d", (int)pid); put_file(OWNHELPER, v);
  r2_slog("selfown: copy is program %d - letting go of the lock, watching it for 3 minutes", (int)pid);
  if (g_lock >= 0) { flock(g_lock, LOCK_UN); close(g_lock); g_lock = -1; }
  for (int i = 0; i < 36; i++) {
    sleep(5);
    if (file_exists(OWNOK)) { r2_slog("selfown: the copy reported to the engine - hand-over done"); for (;;) sleep(3600); }
    if (kill(pid, 0)) { r2_slog("selfown: the copy is gone - the starter brings back a normal helper"); unlink(OWNHELPER); for (;;) sleep(3600); }
  }
  r2_slog("selfown: no report from the copy in 3 minutes - stopping it; the starter brings back a normal helper");
  kill(pid, SIGKILL); unlink(OWNHELPER);
  for (;;) sleep(3600);
}
static void look_kill(const char *spec) {
  int pid = atoi_safe(spec + 7), mine = 0;
  FILE *f = fopen(OWNPID, "r"); char b[24] = "";
  if (f) { if (fgets(b, sizeof b, f)) mine = atoi_safe(b); fclose(f); }
  if (pid < 100 || pid != mine) { look_kv("kill/result", "refused: not the program /_own started"); return; }
  int r = kill(pid, SIGKILL);
  usleep(300000);
  char v[64]; snprintf(v, sizeof v, "kill %d -> %d (errno %d), still there: %s", pid, r, r ? errno : 0, kill(pid, 0) ? "no" : "yes");
  look_kv("kill/result", v);
  unlink(OWNPID);
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
    else if (!strncmp(t, "/_sdel.", 7)) look_sdel(key, t);
    else if (!strncmp(t, "/_sparts.", 9)) look_sparts(t);
    else if (!strncmp(t, "/_gback.", 8)) look_gback(key, t);
    else if (!strncmp(t, "/_gmove.", 8)) look_gmove(key, t);
    else if (!strncmp(t, "/_snap.", 7)) look_snap(key, t);
    else if (!strcmp(t, "/_rest")) look_rest();
    else if (!strcmp(t, "/_reboot")) look_reboot();
    else if (!strncmp(t, "/_ddp.", 6)) look_ddp(t);
    else if (!strncmp(t, "/_wake.", 7)) look_wake(t);
    else if (!strcmp(t, "/_wkey")) look_wkey();
    else if (!strcmp(t, "/_ddpall")) look_ddpall();
    else if (!strncmp(t, "/_wol.", 6)) look_wol(t);
    else if (!strncmp(t, "/_tail.", 7)) look_tail(t + 7, dp);
    else if (!strcmp(t, "/_ps")) look_ps();
    else if (!strncmp(t, "/_thr", 5)) look_thr(t);
    else if (!strcmp(t, "/_sock")) look_sock();
    else if (!strncmp(t, "/_keepawake.", 12)) look_keepawake(t);
    else if (!strncmp(t, "/_rpreg.", 8)) look_rpreg(t);
    else if (!strncmp(t, "/_wakerp.", 9)) look_wakerp(t);
    else if (!strcmp(t, "/_own")) look_own();
    else if (!strcmp(t, "/_selfown")) look_selfown();
    else if (!strncmp(t, "/_kill.", 7)) look_kill(t);
    else if (t[0] == '/' && !strstr(t, "..")) look_dir(t, 0, dp > 6 ? 6 : dp);
  }
  jadd("}}");
  char hdr[200], rbody[200], err[120];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  for (int i = 0; i < 3 && r2tls_request(ENGINE_HOST, "POST", "/ps4/done", hdr, J, Jn, rbody, sizeof rbody, err, sizeof err) != 200; i++) sleep(5);
  if (selfown_after) { selfown_after = 0; sleep(2); do_selfown(); }
  if (reboot_after) {
    reboot_after = 0; sleep(3);
    int rr = sceSystemStateMgrReboot();
    FILE *rf = fopen("/data/round2/rest.log", "a");
    if (rf) { fprintf(rf, "%lld reboot: %d (0x%08x)\n", (long long)time(0), rr, (unsigned)rr); fclose(rf); }
  }
  if (rest_after) {
    rest_after = 0; sleep(3);
    int r = sceSystemStateMgrEnterStandby(0);
    FILE *f = fopen("/data/round2/rest.log", "a");
    if (f) { fprintf(f, "%lld enter rest mode: %d (0x%08x)\n", (long long)time(0), r, (unsigned)r); fclose(f); }
  }
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
  else if (strstr(body, "\"do\":\"dns\"")) do_dns(key, cid, body);
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

/* 1.1.23 (Kai 16 Nov 6:22pm – first remote wake): a PS4 woken over the network starts with NOBODY signed in and waits at
   "press the PS button". If nobody is signed in a few seconds after a wake, sign the room's main profile in (the PS4's own
   auto-login profile) so it lands on the home screen. Signing in only – the profile itself is never changed.
   Returns the call's result, or 1 = somebody was already signed in (woken by a controller), 2 = no main profile known. */
static int wake_login_main(void) {
  UserIdList l; memset(&l, 0, sizeof l);
  if (!sceUserServiceGetLoginUserIdList(&l)) for (int i = 0; i < 4; i++) if (l.userId[i] != -1 && l.userId[i] != 0) return 1;
  uint32_t m = r2_main_user();
  if (!m) return 2;
  return sceUserServiceLogin((int32_t)m);
}
/* 1.1.42 WATCHDOG (17 Nov: Vale's helper sat in 'read' from 2:37pm until the owner forced the PS4 off; Atlas + Bella went
   quiet the same minutes). A second thread looks every 15 s at how long ago the helper last moved (the running clock - it
   stands still in rest mode, so a wake never trips it). Stuck 5 min in a network step -> cut that connection loose (the
   stuck call returns with an error and the helper carries on). Stuck 20 min anywhere -> a helper running as its own
   program restarts (the starter brings a fresh one); in GoldHEN's shared program it can only note it (exiting there would
   take the blocker down too). Every action is written to slow.log. */
extern volatile int r2tls_fd;
double r2_where_age(void); const char *r2_where_now(void);
static void wd_note(const char *fmt, ...) {
  FILE *f = fopen("/data/round2/slow.log", "a"); if (!f) return;
  time_t x = time(0) + 8 * 3600; struct tm tm; gmtime_r(&x, &tm);
  fprintf(f, "%02d/%02d %02d:%02d:%02d watchdog: ", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
  va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f);
}
static void *watchdog(void *arg) {
  (void)arg; int cuts = 0, noted = 0;
  for (;;) {
    sleep(15);
    double age = r2_where_age(); const char *st = r2_where_now();
    if (age < 300) { cuts = 0; noted = 0; continue; }
    int net = !strcmp(st, "read") || !strcmp(st, "send") || !strcmp(st, "connect") || !strcmp(st, "handshake") || !strcmp(st, "lookup");
    int fd = r2tls_fd;
    if (net && fd >= 0 && cuts < 3) { shutdown(fd, SHUT_RDWR); cuts++; wd_note("stuck %d s in '%s' - cut the connection (%d)", (int)age, st, cuts); continue; }
    if (age >= 1200 && g_own && !sv_mounted) { wd_note("stuck %d s in '%s' - restarting the helper", (int)age, st); _exit(3); }
    if (!noted && age >= 600) { noted = 1; wd_note("stuck %d s in '%s'%s", (int)age, st, g_own ? "" : " (shared program: can't restart itself)"); }
  }
  return 0;
}
int main(void) {
  mkdir("/data/round2", 0777);
  mkdir(APP, 0777);
  /* only one helper at a time: hold the lock while running (the starter watches it). Another one has it -> leave quietly. */
  int lk = open(LOCKF, O_RDWR | O_CREAT, 0666), got = 0;
  for (int i = 0; i < 100 && lk >= 0 && !(got = flock(lk, LOCK_EX | LOCK_NB) == 0); i++) usleep(100000); /* 10 s, in 0.1 s steps (1.1.19) */
  { FILE *of = fopen(OWNHELPER, "r"); char ob[24] = ""; /* 1.1.19: am I the copy "/_selfown" started as its own program? */
    if (of) { if (fgets(ob, sizeof ob, of) && atoi_safe(ob) == (int)getpid()) g_own = 1; fclose(of); } }
  if (lk >= 0 && !got) { if (g_own) _exit(0); for (;;) sleep(3600); } /* another helper runs: stay out of the way (returning = exiting, see step_aside) */
  g_lock = lk;
  put_file(APP "/hello", R2_VERSION); /* the starter keeps a new version once it says hello */
  signal(SIGPIPE, SIG_IGN); /* a dropped connection must never stop the helper */
  /* 1.1.13 (16 Nov, Kai): only real "stop" signals step aside. The PS4 sends SIGCONT (19) every time it WAKES from rest –
     catching it made the helper step aside on each wake, and the starter's relaunch often came before BinLoader was up, so
     rooms were left with no helper until a payload was sent by hand. Wake-up, job-control and info signals are left alone. */
  static const int stop_sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGFPE, SIGBUS, SIGSEGV, SIGSYS, SIGTERM };
  for (unsigned i = 0; i < sizeof stop_sigs / sizeof *stop_sigs; i++) signal(stop_sigs[i], on_sig);
  signal(SIGCONT, SIG_DFL); signal(SIGTSTP, SIG_IGN); signal(SIGTTIN, SIG_IGN); signal(SIGTTOU, SIG_IGN); signal(SIGURG, SIG_IGN);
  signal(SIGIO, SIG_IGN); signal(SIGWINCH, SIG_IGN); signal(SIGINFO, SIG_IGN); signal(SIGUSR1, SIG_IGN); signal(SIGUSR2, SIG_IGN);
  sceUserServiceInitialize(0);
  sceNetCtlInit();
  get_console_id();
  char key[128] = "", status[400] = "starting";
  int warned = 0;
  g_up = (long long)time(0);
  drift0 = (long long)time(0) - (long long)mono_s();
  write_status("started, first report in about 10 seconds");
  r2_where_start(R2_VERSION); /* 1.1.26: slow.log gets "the run before was last in: <step>" */
  { pthread_t wt; if (pthread_create(&wt, 0, watchdog, 0) == 0) pthread_detach(wt); else wd_note("could not start"); } /* 1.1.42 */
  update_guard();
  { char st[16] = ""; FILE *f = fopen(SNAPD "/state", "r"); if (f) { if (fgets(st, sizeof st, f) && (!strncmp(st, "running", 7) || !strncmp(st, "paused", 6))) snap_on = 1; fclose(f); } } /* 1.1.41: carry on a snapshot */
  for (unsigned long m = 0;; m++) {
    char run[32];
    set_dns_servers(); /* 1.1.26: our own name lookups ask the PS4's DNS servers first */
    r2_where("title", 0);
    running_title(run, sizeof run);
    r2_where("playing", 0);
    note_playing(run);
    r2_where("loop", 0);
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
        if (m == 0 && st == 200 && !g_own && fw_major() == 9) { /* 1.1.32: become our own program (9.00 only - own-program proven on 9.00, not yet on 11.00) so later updates don't pile up sleeping copies.
             Gated: at most one attempt per hour (marker file), and a failed attempt just leaves us running the normal way. */
          struct stat ms; int recent = !stat(APP "/promote-try", &ms) && (long long)time(0) - ms.st_mtime < 3600;
          if (!recent) { put_file(APP "/promote-try", R2_VERSION); selfown_after = 1; }
        }
      }
    }
    if (g_awake_until) { if ((long long)time(0) < g_awake_until) { sceSystemServicePowerTick(); sceShellCoreUtilResetAutoPowerDownTimer(); } else g_awake_until = 0; }
    if (selfown_after) { selfown_after = 0; sleep(2); do_selfown(); } /* 1.1.33: act on auto-promote here, not only after a look job */
    r2_where("check-in", 1);
    if ((key[0] || read_key(key, sizeof key)) && check_cmd(key)) { report(key, status, sizeof status); write_status(status); }
    if (snap_on && key[0]) { r2_where("snapshot", 1); snap_step(key, 45); } /* 1.1.41: ~45 s of backup per minute */
    for (int t = 0; t < (snap_on ? 5 : 30); t++) { /* one minute = 30 x 2 s; the update guard runs every 2 s */
      if (file_exists(APP "/stop")) step_aside(); /* the starter asked (roll-back) */
      r2_where("guard", 0);
      if (update_guard()) {
        notify("Round2 stopped a PS4 system update. Please don't accept update prompts.");
        if (read_key(key, sizeof key)) { report(key, status, sizeof status); write_status(status); } /* tell the engine now */
      }
      time_t t0 = time(0);
      r2_where("wait", 0);
      sleep(2);
      r2_where("loop", 0);
      long long gap = (long long)(time(0) - t0);
      /* 1.1.20 (Kai 16 Nov 5:37pm: a wake after OUR remote rest wasn't noticed – the PS4 froze during another step, not in
         this sleep): also compare the wall clock with the running clock. If the running clock stands still in rest mode,
         the difference jumps by the time slept, wherever the freeze happened. wake_via says which check saw it. */
      long long drift = (long long)time(0) - (long long)mono_s() - drift0, slept = 0;
      if ((long long)t0 < 1577836800LL && (long long)time(0) >= 1577836800LL) { gap = 0; drift_seen = drift; } /* 1.1.42: the PS4 setting its clock after a cold start (Vale 17 Nov: 01/01 08:00 -> today) is not a wake */
      if (gap > 90) { slept = gap; wake_via = "sleep"; } /* 1.1.14: a 2 s sleep that took minutes */
      else if (drift - drift_seen > 60) { slept = drift - drift_seen; wake_via = "clock"; }
      drift_seen = drift;
      if (slept) { /* the PS4 was in rest mode (frozen) and has just woken */
        wake_n++; wake_slept = slept; wake_at = (long long)time(0);
        sleep(12); /* the network comes back a few seconds after the screen */
        int st = -1;
        if (read_key(key, sizeof key)) {
          st = report(key, status, sizeof status);
          for (int i = 0; st != 200 && i < 6; i++) { sleep(8); st = report(key, status, sizeof status); }
          write_status(status);
        }
        /* 1.1.24: NO automatic sign-in after a wake. 1.1.23 signed the main profile in while the PS4 was still showing its own
           "press the PS button" screen – the profile was in, but that screen then couldn't be left with a controller (Kai,
           16 Nov 6:42pm, owner). wake_login_main() stays for a later, proper way; -2 = switched off. */
        wake_login = -2; (void)wake_login_main;
        { char msg[200]; /* owner 16 Nov: a pop-up after every wake, like GoldHEN's FTP / BinLoader ones */
          snprintf(msg, sizeof msg, "Round2 helper %s: awake after rest mode - %s", R2_VERSION, st == 200 ? "running and connected" : "running (can't reach Round2 yet, will retry)");
          notify(msg); }
        drift_seen = (long long)time(0) - (long long)mono_s() - drift0;
      }
    }
  }
  return 0;
}
