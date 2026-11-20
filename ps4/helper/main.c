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
#include "mbedtls/md.h"

#include "r2tls.h"
#include "shprobe_prx.h" /* 1.1.103 */
#include <ps4/kernel.h>
#include <sys/ioctl.h>
#include <sys/user.h>

/* 20 Nov 6:41pm (owner: "split to get logs faster … once test bench is flawless we can merge"): two builds from one source.
   R2_SHELL=1 (test channel, Kaeden) = 1.1.115 with the menu add-on auto-load + /_shtest; the release (rooms) = 1.1.114 without. */
#ifndef R2_SHELL
#define R2_SHELL 0
#endif
#if defined(R2_V116)
#define R2_VERSION "1.1.116"   /* 20 Nov 7:10pm: round2_shell4 crashed Kaeden's menu on auto-load -> 1.1.116 test = release code, no auto-load */
#elif R2_SHELL
#define R2_VERSION "1.1.124"   /* 1.1.117 shell5 + one try; 1.1.118 shell6, /_shtest end; 1.1.119 shell7 + fg.txt + pop-up after the tile closes; 1.1.120 tile download straight to a file; 1.1.121 bundles via a file + light reports during member loads; 1.1.122 "part already exists" = use it; 1.1.123 pre-load backup to the cloud AFTER the load; 1.1.124 switch = release the other profiles first, fast fg, shell9 */
#else
#define R2_VERSION "1.1.114"
#endif
#define ENGINE_HOST "round2-book.donttelldadyet.workers.dev"
#define VUE_ID "CUSA00960" /* PS Vue + its half-downloaded update keep the Vue jailbreak working – never touch, never count */
#define REPORT_EVERY 15     /* minutes */
/* 1.0.4: run by the starter (round2-boot.elf) – see boot/main.c */
#define APP "/data/round2/app"
static void dns_file_sha(const char *path, char out[16]);
static int file_exists(const char *p);
static void put_file(const char *p, const char *txt);
static void tile_ver(char *v, size_t n); static int tile_check(const char *key, int force); /* 1.1.87 */
static int may_rename(uint32_t uid); /* 1.1.100 */
static int cl_rd(const char *f, char *b, size_t n); /* 1.1.108 */
static int r2_homebrew(const char *t);
static void sw_game_name(const char *t, char *out, size_t n); static void pname(const char *u, char *o, size_t n); /* 1.1.102 plain pop-ups */
static int g_own;
/* 1.1.31: keep a woken PS4 on (power tick) + the 1 MB save buffer, declared early so report() and step_aside() can use them. */
int sceSystemServicePowerTick(void);
int sceShellCoreUtilResetAutoPowerDownTimer(void);
static long long g_awake_until;
static long long g_awake_engine; /* 1.1.68: booking hold from the engine ("aw") */
/* 1.1.69 (owner 19 Nov, speed-up step 1 "measure first"): how long each step really takes, IN MEMORY only (no disk writes):
   count, average, worst, last. Sent with every report as "tm" and readable any time with /_timing. Fixed table, bounded. */
#define TM_MAX 24
static struct { const char *name; unsigned n; double sum, max, last; } g_tm[TM_MAX];
static long long g_tm_since;
void r2_timing(const char *name, double secs) {
  if (!name || secs < 0 || secs > 86400) return;
  int i = 0; for (; i < TM_MAX && g_tm[i].name; i++) if (!strcmp(g_tm[i].name, name)) break;
  if (i == TM_MAX) return;
  if (!g_tm[i].name) g_tm[i].name = name; /* names are string constants */
  g_tm[i].n++; g_tm[i].sum += secs; g_tm[i].last = secs; if (secs > g_tm[i].max) g_tm[i].max = secs;
  if (!g_tm_since) g_tm_since = (long long)time(0);
}
static double tm_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
/* 1.1.41 (owner 17 Nov: back up every save on every PS4 = the "pre day 0" restore point, and prove each PS4 can do it):
   a background job, see "SNAPSHOT" further down. */
#define SNAPD "/data/round2/snap"
static int snap_on, snap_bg, look_mute, sv_noprep, clean_on;
/* 1.1.123 (owner 20 Nov 8:47pm, OK'd): the safety backup before a member load stays ON THE PS4 during the load (it was uploaded
   first: 37 s for 33 MB on Kaeden) and goes to the cloud right after, when no member load runs (defer_tick). A clean slate leaves
   nothing to back up, so then this step costs nothing anyway. */
static int sv_defer; static char sv_defer_dir[300];
static void cloud_report(void); /* 1.1.52 */
static int cd_on, cd_ok; static long long cd_deadline; static char cd_lab[48]; /* 1.1.55 reset countdown */
static long long g_after_load; /* 1.1.119: a member load batch just finished (time) -> pop-up once the tile is gone */
static long long cd_last_in, cd_play_from; static int cd_input = -2; /* 1.1.56: button detector (-2 not tried, -1 not readable, 0/1) */
static int snap_covered(const char *u, const char *t, const char *nm, long long stamp); /* 1.1.52 */
static int clean_status(char *st, int *done, int *total, int *fails, int *parts);
static long long snap_hb;
static int snap_status(char *lab, char *stamp, char *st, int *done, int *total, int *fails, long long *by);
static char *read_all(const char *p, size_t *n, size_t max);
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

/* 1.1.75 (owner 19 Nov 4:48pm, Kaeden test bench: "give feedback through popups so I don't stare at a blank home screen"):
   VERBOSE pop-ups, only while /data/round2/popups-verbose exists ("/_bpop.on" / "/_bpop.off"). Messages go into a small queue;
   a thread shows one every 3 s (the PS4 stacks them otherwise). A progress message with a key replaces the queued one with
   the same key, so a backup shows its latest count instead of a backlog. Full queue: the oldest plain message is dropped. */
#define BPOP_FLAG "/data/round2/popups-verbose"
#define BPOP_Q 8
static pthread_mutex_t bp_mx = PTHREAD_MUTEX_INITIALIZER;
static char bp_msg[BPOP_Q][200], bp_key[BPOP_Q][16]; static int bp_n, bp_on = -1; static long long bp_chk;
static int bpop_enabled(void) {
  long long now = (long long)time(0);
  if (bp_on < 0 || now - bp_chk >= 10) { struct stat st; bp_on = !stat(BPOP_FLAG, &st); bp_chk = now; }
  return bp_on;
}
static void bpop_key(const char *key, const char *fmt, ...) {
  if (!bpop_enabled()) return;
  char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  pthread_mutex_lock(&bp_mx);
  int i = -1;
  if (key && key[0]) for (int j = 0; j < bp_n; j++) if (!strcmp(bp_key[j], key)) { i = j; break; }
  if (i < 0) { if (bp_n == BPOP_Q) { memmove(bp_msg, bp_msg + 1, sizeof bp_msg[0] * (BPOP_Q - 1)); memmove(bp_key, bp_key + 1, sizeof bp_key[0] * (BPOP_Q - 1)); bp_n--; } i = bp_n++; }
  snprintf(bp_msg[i], sizeof bp_msg[i], "%s", b); snprintf(bp_key[i], sizeof bp_key[i], "%s", key ? key : "");
  pthread_mutex_unlock(&bp_mx);
}
#define bpop(...) bpop_key("", __VA_ARGS__)
void r2_bpop(const char *key, const char *msg) { bpop_key(key, "%s", msg); } /* for tls/r2dns.c */
static void *bpop_thread(void *arg) {
  (void)arg;
  for (;;) {
    sleep(3);
    char m[220] = ""; pthread_mutex_lock(&bp_mx);
    if (bp_n) { snprintf(m, sizeof m, "Round2 %s: %s", R2_VERSION, bp_msg[0]); memmove(bp_msg, bp_msg + 1, sizeof bp_msg[0] * (BPOP_Q - 1)); memmove(bp_key, bp_key + 1, sizeof bp_key[0] * (BPOP_Q - 1)); bp_n--; }
    pthread_mutex_unlock(&bp_mx);
    if (m[0]) notify(m);
  }
  return 0;
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
  double t_rep0 = tm_now(); /* 1.1.69 */
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
  { double t = tm_now(); int sb = sony_blocked(); r2_timing("report: sony check", tm_now() - t);
    jadd(",\"sony\":%s,\"running\":", sb ? "\"blocked\"" : "\"OPEN\""); }
  jstr(run);
  add_users();
  add_wifi();
  int fastrep = file_exists("/data/round2/memqn"); /* 1.1.121: a member load runs - reports every few s for the tile's bar; Kaeden's
     disk scan (2.2 s) + games list (1.4 s) made each one ~4 s and slowed the load. Reuse the last ones (max 2 min old). */
  { double t = tm_now(); static char *stc; static long long stat_at;
    if (fastrep && stc && time(0) - stat_at < 120) jadd("%s", stc);
    else { size_t a0 = Jn; add_storage(); free(stc); stc = malloc(Jn - a0 + 1); if (stc) { memcpy(stc, J + a0, Jn - a0); stc[Jn - a0] = 0; stat_at = time(0); } }
    r2_timing("report: disk + folders", tm_now() - t); }
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
  { char tv[24]; tile_ver(tv, sizeof tv); jadd(",\"tile\":\"%s\"", tv); } /* 1.1.87 */
  jadd(",\"up\":%lld", g_up);
  if (g_awake_until) jadd(",\"awakeHold\":%lld", g_awake_until - (long long)time(0));
  if (g_awake_engine) jadd(",\"awakeBook\":%lld", g_awake_engine - (long long)time(0));
  { char cst[16] = ""; int cd = 0, ct = 0, cf = 0, cp = 0; if (clean_status(cst, &cd, &ct, &cf, &cp)) { char rk[64] = ""; FILE *rf = fopen("/data/round2/clean/run", "r"); if (rf) { if (!fgets(rk, sizeof rk, rf)) rk[0] = 0; fclose(rf); } rk[strcspn(rk, " \n")] = 0;
    jadd(",\"clean\":{\"kind\":"); jstr(rk[0] ? rk : "clean"); jadd(",\"state\":"); jstr(cst); jadd(",\"done\":%d,\"total\":%d,\"fails\":%d,\"parts\":%d}", cd, ct, cf, cp); } }
  { int left = 0, of = 0; char b[600] = "", nm[48] = ""; FILE *f = fopen("/data/round2/memq", "r"); if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); for (char *p = b; *p; p++) if (*p == '\n') left++; }
    f = fopen("/data/round2/memqn", "r"); if (f) { if (fscanf(f, "%d %47s", &of, nm) < 1) of = 0; fclose(f); } for (char *c = nm; *c; c++) if (*c == '_') *c = ' ';
    if (of > 0) { jadd(",\"memq\":{\"left\":%d,\"of\":%d,\"now\":", left, of); jstr(nm); jadd("}"); } } /* 1.1.101: players still waiting in a member load */
  cloud_report(); /* 1.1.52 */
  if (cd_on) { jadd(",\"countdown\":{\"left\":%lld,\"lab\":", cd_deadline - (long long)time(0)); jstr(cd_lab); jadd(",\"ok\":%d,\"input\":%d,\"play\":%lld}", cd_ok, cd_input, cd_play_from ? (long long)time(0) - cd_play_from : 0LL); } /* 1.1.55/56 */
  { char lab[24] = "", stp[24] = "", sst[16] = ""; int dn = 0, tt = 0, fl = 0; long long by = 0;
    if (snap_status(lab, stp, sst, &dn, &tt, &fl, &by)) { jadd(",\"snap\":{\"label\":"); jstr(lab); jadd(",\"stamp\":"); jstr(stp); jadd(",\"state\":"); jstr(sst);
      jadd(",\"done\":%d,\"total\":%d,\"fails\":%d,\"mb\":%lld}", dn, tt, fl, by / 1048576); } }
  if (wake_n) jadd(",\"woke\":{\"n\":%d,\"at\":%lld,\"sleptS\":%lld,\"via\":\"%s\",\"login\":%d}", wake_n, wake_at, wake_slept, wake_via, wake_login);
  tail_n = 0; jadd(",\"log\":{"); add_tail("boot", APP "/boot.log", 8); add_tail("stopped", "/data/round2/helper-stopped.txt", 3); add_tail("slow", "/data/round2/slow.log", 6); add_tail("netfail", "/data/round2/net-fail.log", 8); add_tail("save", "/data/round2/save.log", 10); add_tail("clean", "/data/round2/clean/history.txt", 6); add_tail("cleanerr", "/data/round2/clean/errors.txt", 6); jadd("}");
  jadd(",\"played\":{");
  for (int k = 0; k < nplayed; k++) jadd("%s\"%s\":%d", k ? "," : "", played_id[k], played_min[k]);
  jadd("}");
  uint64_t gh = games_sent_hash;
  if (!fastrep) { /* 1.1.121: no games list while a member load runs (it doesn't change then) */
  char *g = 0;
  double tgj = tm_now();
  gh = games_json(&g);
  r2_timing("report: games list", tm_now() - tgj);
  if (gh != games_sent_hash) jadd(",\"games\":%s", g);
  free(g); }
  if (g_tm_since) { /* 1.1.69: timings so far - name: [count, average ms, worst ms, last ms] */
    jadd(",\"tm\":{\"since\":%lld", g_tm_since);
    for (int i = 0; i < TM_MAX && g_tm[i].name; i++) jadd(",\"%s\":[%u,%.0f,%.0f,%.0f]", g_tm[i].name, g_tm[i].n, g_tm[i].sum / g_tm[i].n * 1000, g_tm[i].max * 1000, g_tm[i].last * 1000);
    jadd("}");
  }
  jadd("}");
  r2_timing("report: build (before sending)", tm_now() - t_rep0);

  char hdr[200], body[512], err[200];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  int st = r2tls_request(ENGINE_HOST, "POST", "/ps4/in", hdr, J, Jn, body, sizeof body, err, sizeof err);
  if (st == 200) {
    games_sent_hash = gh;
    nplayed = 0; /* counted by the engine */
    if (g_own && !file_exists(OWNOK)) put_file(OWNOK, R2_VERSION); /* tells the helper that started us: all good */
  }
  snprintf(status, slen, "%d %s %.200s", st, err, body);
  r2_timing("report: total", tm_now() - t_rep0);
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
/* 1.1.61 (18 Nov 3:30am, owner: "overflow?"): starting a helper as its own PS4 program (elfldr spawn) can FREEZE the whole PS4
   once it has been done many times since the PS4 last started - Kai froze on its 15th spawn since its restart (3:04:46am,
   own-spawn.log stops at "spawning"; the very same file started fine after the restart), Max on its 6th (11:42pm, the
   controller-library build). Something in the system builds up per spawn and only a PS4 restart clears it. So: at most
   OWN_SPAWN_MAX spawns per PS4 start; after that an update waits for the next restart. Normal days need 1-2. */
#define SPAWNCNT "/data/round2/own-spawns.txt"
#define OWN_SPAWN_MAX 5
static long long boot_id(void) {
  struct timeval tv; size_t l = sizeof tv; memset(&tv, 0, sizeof tv);
  if (!sysctlbyname("kern.boottime", &tv, &l, 0, 0) && tv.tv_sec > 0) return tv.tv_sec;
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (long long)time(0) - ts.tv_sec;
}
static int own_spawns(void) { /* spawns since this PS4 started */
  FILE *f = fopen(SPAWNCNT, "r"); long long b = 0; int n = 0;
  if (f) { if (fscanf(f, "%lld %d", &b, &n) != 2) n = 0; fclose(f); }
  long long now = boot_id();
  return (b > now - 300 && b < now + 300) ? n : 0;
}
static void own_spawn_add(void) { char t[48]; int n = own_spawns() + 1; snprintf(t, sizeof t, "%lld %d", boot_id(), n); put_file(SPAWNCNT, t); }
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
  own_spawn_add(); r2_slog("own-update: spawn %d since this PS4 started", own_spawns());
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
  if (run[0] && strcmp(run, VUE_ID) && strcmp(run, "RTWO00001")) { cmd_done(key, cid, 0, 1, 0, 0, 0, "a game is open"); return; } /* 1.1.90: the tile open is fine */
  if (!v[0] || strlen(sha) != 64 || atoi_safe(sz) < 1024) { cmd_done(key, cid, 0, 0, 0, 0, 0, "bad update details"); return; }
  if (!strcmp(v, R2_VERSION)) { cmd_done(key, cid, 1, 0, 0, 0, 0, "already on this version"); return; }
  if (g_own && own_spawns() >= OWN_SPAWN_MAX) { char w[120]; snprintf(w, sizeof w, "waits for the next PS4 restart (%d switches since it started - more can freeze it)", own_spawns()); cmd_done(key, cid, 0, 1, 0, 0, 0, w); return; }
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
static char look_last[240]; /* 1.1.53: with look_mute on, the last result is kept here for the job that called */
static void look_kv(const char *k, const char *v) { if (look_mute) { snprintf(look_last, sizeof look_last, "%s: %s", k, v); return; } if (look_n++ < 400) { jadd("%s", look_n > 1 ? "," : ""); jstr(k); jadd(":"); jstr(v); } }
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
static int sv_game_open(void) { char run[32]; running_title(run, sizeof run); return run[0] && strcmp(run, VUE_ID) && strcmp(run, "RTWO00001"); } /* 1.1.90: the Round2 tile open is not a game (saves load while it shows progress) */

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
  if (bpop_enabled() && file_exists("/data/round2/popups-tech")) { char b[200]; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); bpop_key(!strncmp(b, "backup ", 7) || !strncmp(b, "cloud batch: sent", 17) || !strncmp(b, "copy only: ", 11) ? "bk" : "", "%s", b); } /* 1.1.75; 1.1.76 per-part lines share one slot */
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
      if (!sv_defer && sv_put(key, k, sv_buf, got)) { bad = 1; break; }   /* 1.1.123: deferred = on the PS4 only for now */
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
static void put_file2(const char *dir, const char *name, const char *txt) { char p[400]; snprintf(p, sizeof p, "%s/%s", dir, name); FILE *f = fopen(p, "w"); if (f) { fputs(txt, f); fclose(f); } }
static int rm_tree2(const char *p, int depth) { struct stat st; if (lstat(p, &st)) return 0; if (!S_ISDIR(st.st_mode)) return unlink(p);
  DIR *d = opendir(p); if (!d) return -1; struct dirent *e; char q[600]; int bad = 0;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; snprintf(q, sizeof q, "%s/%s", p, e->d_name); if (depth < 10 && rm_tree2(q, depth + 1)) bad = 1; }
  closedir(d); return rmdir(p) || bad; }
/* 1.1.123: upload ONE deferred pre-load backup (all its parts), then remove it. Only when no member load runs. */
static int defer_up_dir(const char *key, const char *base, const char *dir, const char *rel, int depth) {   /* files -> base/f/<rel>~<piece> */
  DIR *d = opendir(dir); if (!d) return -1; struct dirent *e; char p[600], r[400], k[800]; int bad = 0;
  while (!bad && (e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (!rel[0] && (!strcmp(e->d_name, "manifest.txt") || !strcmp(e->d_name, "cloud.txt"))) continue;
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name); snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
    struct stat st; if (lstat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) { if (depth < 8 && defer_up_dir(key, base, p, r, depth + 1)) bad = 1; continue; }
    int fd = open(p, O_RDONLY); if (fd < 0) { bad = 1; break; }
    unsigned long long sz = (unsigned long long)st.st_size, done = 0; int pc = 0;
    do { size_t want = sz - done > SV_PIECE ? SV_PIECE : (size_t)(sz - done), got = 0;
      while (got < want) { ssize_t x = read(fd, sv_buf + got, want - got); if (x <= 0) break; got += (size_t)x; }
      if (got != want) { bad = 1; break; }
      snprintf(k, sizeof k, "%s/f/%s~%d", base, r, pc); if (sv_put(key, k, sv_buf, got)) { bad = 1; break; }
      done += got; pc++; } while (done < sz);
    close(fd); }
  closedir(d); return bad;
}
static void defer_tick(const char *key) {
  static long long last; if (time(0) - last < 20) return; last = time(0);
  if (file_exists("/data/round2/memqn") || clean_on) return;
  if (!sv_buf) { void *b = mmap(0, SV_PIECE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (b == MAP_FAILED) return; sv_buf = b; }
  DIR *d = opendir("/data/round2/defer"); if (!d) return; struct dirent *e; char top[400] = "";
  while ((e = readdir(d))) { if (e->d_name[0] == '.') continue; char rp[400]; struct stat st; snprintf(rp, sizeof rp, "/data/round2/defer/%s/ready.txt", e->d_name); if (!stat(rp, &st)) { snprintf(top, sizeof top, "/data/round2/defer/%s", e->d_name); break; } }
  closedir(d); if (!top[0]) return;
  r2_where("job deferred backup", 1);
  double t0 = tm_now(); int bad = 0, parts = 0; DIR *pd = opendir(top); struct dirent *pe; char p[600];
  while (pd && !bad && (pe = readdir(pd))) { if (pe->d_name[0] == '.') continue; snprintf(p, sizeof p, "%s/%s", top, pe->d_name); struct stat st; if (lstat(p, &st) || !S_ISDIR(st.st_mode)) continue;
    char cf[700], base[300] = ""; snprintf(cf, sizeof cf, "%s/cloud.txt", p); FILE *f = fopen(cf, "r"); if (!f || !fgets(base, sizeof base, f)) { if (f) fclose(f); bad = 1; break; } fclose(f); base[strcspn(base, "\r\n")] = 0;
    if (defer_up_dir(key, base, p, "", 0)) { bad = 1; break; }
    size_t n = 0; char mp[700]; snprintf(mp, sizeof mp, "%s/manifest.txt", p); char *m = read_all(mp, &n, 4 << 20); char mk[400]; snprintf(mk, sizeof mk, "%s/manifest.txt", base);
    if (!m || sv_put(key, mk, (const unsigned char *)m, n)) { free(m); bad = 1; break; } free(m); parts++; }
  if (pd) closedir(pd);
  if (!bad) { size_t n = 0; char gp[500], kp[500], gk[400] = ""; snprintf(gp, sizeof gp, "%s/game.txt", top); snprintf(kp, sizeof kp, "%s/gamekey.txt", top);
    FILE *f = fopen(kp, "r"); if (f) { if (!fgets(gk, sizeof gk, f)) gk[0] = 0; fclose(f); } gk[strcspn(gk, "\r\n")] = 0;
    char *g = read_all(gp, &n, 1 << 20); if (!gk[0] || !g || sv_put(key, gk, (const unsigned char *)g, n)) bad = 1; free(g); }
  sv_log("deferred backup %s: %s - %d part(s) in %.0f s", bad ? "NOT sent (tries again in 20 s)" : "sent to the cloud", top, parts, tm_now() - t0);
  if (!bad) rm_tree2(top, 0);
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
    if (sv_defer) { snprintf(sv_prep, sizeof sv_prep, "%s/%s", sv_defer_dir, nm); mkdirs_for(sv_prep); mkdir(sv_prep, 0777); } /* 1.1.123 */
    else if (sv_noprep) sv_prep[0] = 0; else { snprintf(sv_prep, sizeof sv_prep, "/data/round2/prep/%s/%s/%s", u, t, nm); mkdirs_for(sv_prep); } sv_prep_fail = 0;
    sv_log("backup start: user %s game %s save %s -> prep %s + cloud %s", u, t, nm, sv_prep, base);
    sv_files = sv_fail = 0; sv_bytes = 0; sv_man_n = 0; if (sv_man) sv_man[0] = 0;
    sv_up_dir(key, base, SV_MNT, "", 0);
    int cr = sv_close();
    char mk[260]; snprintf(mk, sizeof mk, "%s/manifest.txt", base);
    int mr = sv_fail ? -1 : sv_defer ? (sv_prep_fail ? -1 : 0) : sv_put(key, mk, (const unsigned char *)(sv_man ? sv_man : ""), sv_man_n);   /* 1.1.123: deferred = the local copy must be whole */
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
/* 1.1.85 (owner 19 Nov 8:25pm: "test the new idea vs the earlier one"): PER-GAME PREFETCH. Before a game's parts are made and
   filled, every part's manifest, label files and small data files come down in a few bundle requests and wait here in memory;
   sv_get / sv_getm answer from here first. Cleared after each game. Off with "/_pf.off" (for the A/B test). */
#define PF_MAX 512
static struct { char *k; unsigned char *d; size_t l; } pf[PF_MAX]; static int pf_n; static size_t pf_bytes; static int pf_hits;
static void pf_clear(void) { for (int i = 0; i < pf_n; i++) { free(pf[i].k); free(pf[i].d); } pf_n = 0; pf_bytes = 0; }
static int pf_find(const char *k) { for (int i = 0; i < pf_n; i++) if (!strcmp(pf[i].k, k)) return i; return -1; }
static void pf_add(const char *k, unsigned char *d, size_t l) { if (pf_n >= PF_MAX || pf_bytes + l > (24u << 20)) { free(d); return; } pf[pf_n].k = strdup(k); pf[pf_n].d = d; pf[pf_n].l = l; if (!pf[pf_n].k) { free(d); return; } pf_n++; pf_bytes += l; }
static int pf_take(const char *k, unsigned char **data, size_t *len) { int i = pf_find(k); if (i < 0) return -1; *data = malloc(pf[i].l + 1); if (!*data) return -1; memcpy(*data, pf[i].d, pf[i].l); *len = pf[i].l; pf_hits++; return 0; }
/* 1.1.81: the last manifest fetched is kept for 2 min - making a part and then filling it both read the same manifest.txt */
static char mc_key[400]; static unsigned char *mc_data; static size_t mc_len; static long long mc_at;
static int sv_get_raw(const char *key, const char *k, unsigned char **data, size_t *len, char *why, size_t wn);
static int sv_get(const char *key, const char *k, unsigned char **data, size_t *len, char *why, size_t wn) {
  if (!pf_take(k, data, len)) return 0; /* 1.1.85 */
  size_t kl = strlen(k); int ism = kl > 13 && !strcmp(k + kl - 13, "/manifest.txt");
  if (ism && mc_data && !strcmp(mc_key, k) && (long long)time(0) - mc_at < 120) { *data = malloc(mc_len + 1); if (!*data) return -1; memcpy(*data, mc_data, mc_len); *len = mc_len; return 0; }
  int r = sv_get_raw(key, k, data, len, why, wn);
  if (!r && ism && *len < 256 * 1024 && kl < sizeof mc_key) { free(mc_data); mc_data = malloc(*len + 1); if (mc_data) { memcpy(mc_data, *data, *len); mc_len = *len; snprintf(mc_key, sizeof mc_key, "%s", k); mc_at = (long long)time(0); } }
  return r;
}
/* 1.1.81 (owner 19 Nov 7pm, Kaeden: 77% of making a save part was fetching its small label files one request at a time, ~0.3 s
   each): many cloud pieces in ONE request (/ps4/sgetm, engine reads them in parallel). Returns 0 (data[i] malloc'd, or 0 if
   that piece is missing), -2 = this engine doesn't know /ps4/sgetm yet (caller falls back to one by one), -1 = failed. */
static int sv_getm_net(const char *key, const char **ks, int n, unsigned char **data, size_t *lens, char *why, size_t wn);
static int sv_getm(const char *key, const char **ks, int n, unsigned char **data, size_t *lens, char *why, size_t wn) {
  for (int i = 0; i < n; i++) { data[i] = 0; lens[i] = 0; }
  if (n <= 0) return 0;
  const char *miss[64]; int mi[64], nm = 0, nb = 0;
  for (int i = 0; i < n; i++) { if (!pf_take(ks[i], &data[i], &lens[i])) continue; if (nm < 64) { miss[nm] = ks[i]; mi[nm] = i; nm++; } else nb = 1; }
  if (!nm && !nb) return 0;
  if (nb) { for (int i = 0; i < n; i++) { free(data[i]); data[i] = 0; } return sv_getm_net(key, ks, n, data, lens, why, wn); }
  unsigned char *md[64]; size_t ml[64]; int r = sv_getm_net(key, miss, nm, md, ml, why, wn);
  if (r) { for (int i = 0; i < n; i++) { free(data[i]); data[i] = 0; } return r; }
  for (int j = 0; j < nm; j++) { data[mi[j]] = md[j]; lens[mi[j]] = ml[j]; }
  return 0;
}
static int sv_getm_net(const char *key, const char **ks, int n, unsigned char **data, size_t *lens, char *why, size_t wn) {
  for (int i = 0; i < n; i++) { data[i] = 0; lens[i] = 0; }
  if (n <= 0) return 0;
  size_t bc = 16; for (int i = 0; i < n; i++) bc += strlen(ks[i]) * 2 + 4;
  char *body = malloc(bc); if (!body) return -1;
  size_t bn = 0; bn += (size_t)snprintf(body + bn, bc - bn, "{\"k\":[");
  for (int i = 0; i < n; i++) { body[bn++] = i ? ',' : '"'; if (i) body[bn++] = '"';
    for (const char *c = ks[i]; *c; c++) { if (*c == '"' || *c == '\\') body[bn++] = '\\'; body[bn++] = *c; } body[bn++] = '"'; }
  bn += (size_t)snprintf(body + bn, bc - bn, "]}");
  char hdr[200], err[120]; snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  /* 1.1.121 (owner 20 Nov 8:40pm "what happened to our optimisations"): the bundle (up to 6 MB) came into memory, and the reply
     buffer's 4 MB -> 8 MB step failed on Kaeden ("bundle download -1 no memory" -> piece by piece, 36 s for 32 MB). Now the
     bundle goes into a file and each piece (1 MB) is read back on its own - no big memory needed. */
  const char *bf = "/data/round2/bundle.tmp"; int st = -1; size_t dl = 0;
  for (int a = 0; a < 3; a++) { err[0] = 0; dl = 0; FILE *f = fopen(bf, "wb"); if (!f) { st = -1; snprintf(err, sizeof err, "can't create %s", bf); break; }
    st = r2tls_fetch_file(ENGINE_HOST, "POST", "/ps4/sgetm", hdr, body, bn, f, &dl, err, sizeof err); fclose(f); if (st == 200 || st == 404) break; sleep(1); }
  free(body);
  if (st == 404) { unlink(bf); return -2; }
  if (st != 200) { unlink(bf); if (why) snprintf(why, wn, "bundle download %d %s", st, err); return -1; }
  FILE *f = fopen(bf, "rb"); if (!f) { if (why) snprintf(why, wn, "bundle: can't read it back"); return -1; }
  char line[32]; int cnt = -1;
  if (!fgets(line, sizeof line, f) || memcmp(line, "R2M ", 4)) { fclose(f); unlink(bf); if (why) snprintf(why, wn, "bundle: bad answer"); return -1; }
  cnt = atoi_safe(line + 4);
  if (cnt != n) { fclose(f); unlink(bf); if (why) snprintf(why, wn, "bundle: %d of %d", cnt, n); return -1; }
  for (int i = 0; i < n; i++) {
    if (!fgets(line, sizeof line, f)) { for (int j = 0; j < i; j++) { free(data[j]); data[j] = 0; } fclose(f); unlink(bf); if (why) snprintf(why, wn, "bundle: cut short"); return -1; }
    long long L = atoll(line); if (L < 0) continue;
    data[i] = malloc((size_t)L + 1); if (!data[i]) { for (int j = 0; j < i; j++) { free(data[j]); data[j] = 0; } fclose(f); unlink(bf); if (why) snprintf(why, wn, "bundle: no memory for a piece"); return -1; }
    if (fread(data[i], 1, (size_t)L, f) != (size_t)L) { for (int j = 0; j <= i; j++) { free(data[j]); data[j] = 0; } fclose(f); unlink(bf); if (why) snprintf(why, wn, "bundle: cut short"); return -1; }
    lens[i] = (size_t)L;
  }
  fclose(f); unlink(bf); (void)dl; return 0;
}
static int sv_get_raw(const char *key, const char *k, unsigned char **data, size_t *len, char *why, size_t wn) {
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
/* 1.1.77 (owner 19 Nov 5:17pm: how long does a member's download take?): seconds spent per step of a move, summed. */
static double g_t_dl, g_t_wr, g_t_bk, g_t_mk; static unsigned long long g_b_dl;
static int look_srest(const char *key, const char *spec) { /* "/_srest.<user>.<title>.<save>.<backup path>" */
  char v[220], u[16] = "", t[16] = "", nm[48] = "", src[200];
  { /* 1.1.36: save names can contain dots (Cat Quest II "save-file1.dat", PS Vue "localstorage.aes"), so the name ends where the
       backup path starts: the last ".<32-hex console id>/" in the spec. */
    char rest[300] = "";
    if (sscanf(spec, "/_srest.%8[0-9a-f].%9[A-Z0-9].%299[^|]", u, t, rest) != 3) { look_kv("restore/error", "bad spec"); return -1; }
    char *cut = 0;
    for (char *q = rest; (q = strchr(q, '.')); q++) { int h = 0; while (h < 32 && ((q[1 + h] >= '0' && q[1 + h] <= '9') || (q[1 + h] >= 'a' && q[1 + h] <= 'f'))) h++; if (h == 32 && q[33] == '/') cut = q; }
    if (!cut) { char *mp = strstr(rest, ".members/"); if (mp) cut = mp; } /* 1.1.84: a member's folder */
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
  /* 1.1.78 (owner 19 Nov 5:25pm: "what if the internet drops?"): TWO PHASES. 1) every file is downloaded to the PS4's own
     storage (prep) and checked complete - the save is not touched yet, so a drop here leaves the profile exactly as it was.
     2) only when ALL files are here: open the save, clear its old files, copy everything in (local, seconds). Before 1.1.78
     the save was cleared first and filled file by file, so a drop halfway left a broken save. */
  int ok = -1, files = 0, fail = 0, skipped = 0, nl = 0;
  unsigned long long bytes = 0; int wiped = 0;
  for (char *q = mt; *q; q++) if (*q == '\n') nl++;
  char *mt0 = strdup(mt); /* the manifest as downloaded (mt is cut up below) */
  char **fn = calloc((size_t)nl + 1, sizeof *fn); unsigned long long *fsz = calloc((size_t)nl + 1, sizeof *fsz); int nf = 0;
  if (!fn || !fsz) { free(fn); free(fsz); free(mt); free(mt0); sv_rights_back(); look_kv("restore/error", "no memory"); return -1; }
  int *fpc = calloc((size_t)nl + 1, sizeof *fpc);
  for (char *ln = mt, *nx; ln && *ln && fpc; ln = nx) { /* list the files */
    nx = strchr(ln, '\n'); if (nx) *nx++ = 0;
    char *t1 = strchr(ln, '\t'); if (!t1) continue; *t1++ = 0;
    char *t2 = strchr(t1, '\t'); if (!t2) continue; *t2++ = 0;
    if (!strncmp(ln, "sce_sys/", 8) || strstr(ln, "..")) { skipped++; continue; } /* the receiving save keeps its own labels */
    fn[nf] = ln; fsz[nf] = strtoull(t1, 0, 10); fpc[nf] = atoi_safe(t2); nf++;
  }
  if (!fpc) { fail++; snprintf(first_err, sizeof first_err, "no memory"); }
  /* phase 1: download. 1.1.81: one-piece files go in bundles (up to 32 files / 6 MB a request), big files piece by piece */
  int bundles_ok = 1; double tdl0 = tm_now();
  for (int i = 0; i < nf && !fail; ) {
    if (fpc[i] != 1 || !bundles_ok) { i++; continue; }
    static char bk[32][700]; const char *bkp[32]; int bi[32], nb = 0; unsigned long long bsz = 0;
    for (int j = i; j < nf && nb < 32; j++) { if (fpc[j] != 1) continue; if (nb && bsz + fsz[j] > (6u << 20)) break;
      snprintf(bk[nb], sizeof bk[nb], "%s/f/%s~0", src, fn[j]); bkp[nb] = bk[nb]; bi[nb] = j; bsz += fsz[j]; nb++; }
    static unsigned char *bd[32]; static size_t bl[32]; char gw[130] = "";
    int gr = sv_getm(key, bkp, nb, bd, bl, gw, sizeof gw);
    if (gr == -2) { bundles_ok = 0; continue; } /* old engine: piece by piece below */
    if (gr) { fail++; snprintf(first_err, sizeof first_err, "%s", gw); break; }
    for (int q = 0; q < nb; q++) {
      int j = bi[q]; char tmp[520]; snprintf(tmp, sizeof tmp, "%s/%s", prepbase, fn[j]); mkdirs_for(tmp);
      if (!fail && (!bd[q] || bl[q] != fsz[j])) { fail++; snprintf(first_err, sizeof first_err, "%s: got %zu of %llu bytes", fn[j], bd[q] ? bl[q] : 0, fsz[j]); }
      if (!fail) { int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666); ssize_t w = fd >= 0 ? write(fd, bd[q], bl[q]) : -1; if (fd >= 0) { fsync(fd); close(fd); }
        if (w != (ssize_t)bl[q]) { fail++; snprintf(first_err, sizeof first_err, "%s: write %zd of %zu (errno %d)", fn[j], w, bl[q], errno); unlink(tmp); } else { g_b_dl += bl[q]; fpc[j] = -1; } }
      free(bd[q]); bd[q] = 0;
    }
    i = bi[nb - 1] + 1;
  }
  for (int i = 0; i < nf && !fail; i++) { /* the rest (big files, or no bundles): piece by piece */
    if (fpc[i] < 0) continue;
    char tmp[520], k[700]; snprintf(tmp, sizeof tmp, "%s/%s", prepbase, fn[i]); mkdirs_for(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666), bad = fd < 0; char why[160] = "";
    if (bad) snprintf(why, sizeof why, "%s: can't create (errno %d)", fn[i], errno);
    unsigned long long got = 0;
    /* 1.1.86 (owner 19 Nov 8:39pm): a big file's 1 MB pieces in bundles of up to 6 ("/_bb.off" = one by one, for the A/B test) */
    struct stat bbst; int bb = bundles_ok && stat("/data/round2/bigbundle-off", &bbst);
    int pdone = 0; /* 1.1.93: pieces already written - the one-by-one loop carries on from here */
    for (int pi = 0; bb && !bad && pi < fpc[i]; ) {
      int m = fpc[i] - pi > 6 ? 6 : fpc[i] - pi; static char pk[6][700]; const char *pkp[6]; unsigned char *pd[6]; size_t pl[6]; char gw[130] = "";
      for (int j = 0; j < m; j++) { snprintf(pk[j], sizeof pk[j], "%s/f/%s~%d", src, fn[i], pi + j); pkp[j] = pk[j]; }
      int gr = sv_getm(key, pkp, m, pd, pl, gw, sizeof gw);
      if (gr == -2) { bb = 0; break; }
      if (gr) { sv_log("restore: %s pieces %d-%d bundle failed (%s) - carrying on one piece at a time", fn[i], pi, pi + m - 1, gw); bb = 0; break; } /* 1.1.93: was a hard fail (Kaeden 20 Nov 12:46am: "bundle: cut short" -> Vampire Survivors half loaded = "corrupted") */
      for (int j = 0; j < m; j++) {
        if (!bad && !pd[j]) { bad = 1; snprintf(why, sizeof why, "%s piece %d missing in the cloud", fn[i], pi + j); }
        if (!bad) { ssize_t wr = write(fd, pd[j], pl[j]); if (wr != (ssize_t)pl[j]) { bad = 1; snprintf(why, sizeof why, "%s piece %d: write %zd of %zu (errno %d)", fn[i], pi + j, wr, pl[j], errno); } else got += pl[j]; }
        free(pd[j]);
      }
      pi += m; if (!bad) pdone = pi;
      if (pi >= fpc[i]) { fpc[i] = -2; break; }
    }
    for (int pi = pdone; fpc[i] > 0 && !bad && pi < fpc[i]; pi++) {
      unsigned char *d = 0; size_t dl = 0; char gw[130] = "";
      snprintf(k, sizeof k, "%s/f/%s~%d", src, fn[i], pi);
      if (sv_get(key, k, &d, &dl, gw, sizeof gw)) { bad = 1; free(d); snprintf(why, sizeof why, "%s piece %d: %s", fn[i], pi, gw); break; }
      ssize_t wr = write(fd, d, dl);
      if (wr != (ssize_t)dl) { bad = 1; snprintf(why, sizeof why, "%s piece %d: write %zd of %zu (errno %d)", fn[i], pi, wr, dl, errno); }
      got += dl; free(d);
    }
    if (fd >= 0) { fsync(fd); close(fd); }
    g_b_dl += got;
    if (!bad && got != fsz[i]) { bad = 1; snprintf(why, sizeof why, "%s: got %llu of %llu bytes", fn[i], got, fsz[i]); }
    if (bad) { unlink(tmp); fail++; sv_log("restore download fail %s: %s", fn[i], why); if (!first_err[0]) snprintf(first_err, sizeof first_err, "%s", why); break; }
  }
  g_t_dl += tm_now() - tdl0;
  free(fpc);
  if (fail) sv_log("restore: download incomplete for user %s game %s save %s - the save was NOT touched (%s)", u, t, nm, first_err);
  else ok = sv_open(u, t, nm, 0, v, sizeof v);
  if (!fail && ok == 0) { /* phase 2: everything is here - into the save */
    double twr0 = tm_now();
    wiped = sv_wipe_data(SV_MNT, ""); /* 1.1.35: clear the receiving save's old game files (keep sce_sys) so this replaces, not merges */
    sv_log("restore: all %d file(s) downloaded and checked - cleared %d old file(s) from the receiving save (kept sce_sys)", nf, wiped);
    for (int i = 0; i < nf; i++) {
      char out[512], tmp[520]; snprintf(out, sizeof out, "%s/%s", SV_MNT, fn[i]); snprintf(tmp, sizeof tmp, "%s/%s", prepbase, fn[i]); mkdirs_for(out);
      int in = open(tmp, O_RDONLY), of = in >= 0 ? open(out, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
      unsigned long long cp = 0; int cbad = in < 0 || of < 0, cerr = cbad ? errno : 0;
      while (!cbad) { ssize_t x = read(in, sv_buf, SV_PIECE); if (x < 0) { cbad = 1; cerr = errno; break; } if (x == 0) break;
        if (write(of, sv_buf, (size_t)x) != x) { cbad = 1; cerr = errno; break; } cp += (unsigned long long)x; }
      if (of >= 0) { fsync(of); close(of); }
      if (in >= 0) close(in);
      if (cbad || cp != fsz[i]) { if (!first_err[0]) snprintf(first_err, sizeof first_err, "%s: copy into save %llu of %llu (errno %d)", fn[i], cp, fsz[i], cerr); sv_log("restore copy-into-save fail %s: %llu of %llu (errno %d)", fn[i], cp, fsz[i], cerr); fail++; continue; }
      files++; bytes += fsz[i];
    }
    g_t_wr += tm_now() - twr0;
    int cr = sv_close();
    { FILE *cf = fopen("/dev/null","r"); (void)cf; char pmf[300]; snprintf(pmf,sizeof pmf,"%s/cloud-manifest.txt",prepbase); /* 1.1.78: NOT manifest.txt - that is the internal copy's fingerprint list (cloud batch) */ FILE *pf=fopen(pmf,"w"); if(pf){ if(mt0) fputs(mt0,pf); fclose(pf);} char cfp[300]; snprintf(cfp,sizeof cfp,"%s/cloud.txt",prepbase); FILE *c3=fopen(cfp,"w"); if(c3){ fprintf(c3,"%s\n",src); fclose(c3);} }
    sv_log("restore done: user %s game %s save %s: %d files, %llu bytes, %d failed, %d kept, %d cleared%s%s", u, t, nm, files, bytes, fail, skipped, wiped, first_err[0]?", first problem: ":"", first_err);
    snprintf(v, sizeof v, "%d files, %llu bytes, %d failed, %d kept (sce_sys), %d cleared, closed %d", files, bytes, fail, skipped, wiped, cr);
    { char kk[80]; snprintf(kk, sizeof kk, "restore/%s/result", nm); look_kv(kk, v); if (first_err[0]) { snprintf(kk, sizeof kk, "restore/%s/first-problem", nm); look_kv(kk, first_err); } }
    ok = fail ? -1 : 0;
  } else if (fail) { ok = -1; look_kv("restore/error", "download incomplete - the save was not touched"); if (first_err[0]) look_kv("restore/first-problem", first_err); }
  else { look_kv("restore/error", v); sv_log("restore FAILED to open: user %s game %s save %s: %s", u, t, nm, v); }
  sv_rights_back();
  free(mt); free(mt0); free(fn); free(fsz);
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
  /* 1.1.52 BUG FIX (17 Nov 8pm): this used to delete the part's internal copy too (1.1.40, when the cloud copy came first).
     With INTERNAL FIRST the internal copy IS the backup - it stays until the cloud batch has it (and after). */
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
  int gr = fails ? -1 : 0;
  if (!fails && sv_defer) { char lp[400]; snprintf(lp, sizeof lp, "%s/game.txt", sv_defer_dir); FILE *gf = fopen(lp, "w"); if (gf) { fputs(list, gf); fclose(gf); } else gr = -1;
    snprintf(lp, sizeof lp, "%s/gamekey.txt", sv_defer_dir); gf = fopen(lp, "w"); if (gf) { fprintf(gf, "%s\n", gk); fclose(gf); } else gr = -1; }   /* 1.1.123 */
  else if (!fails) gr = sv_put(key, gk, (const unsigned char *)list, strlen(list));
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
static int sv_make_part(const char *key, const char *u, const char *t, const char *nm, const char *src, char *why, size_t wn);
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
  if (nmiss) { char dp[200]; struct stat dst; db_path(u, dp, sizeof dp);
    if (stat(dp, &dst)) { look_kv("move/refused", "profile has no save list yet - open any game once on it"); look_kv("move/missing", missing);
      sv_log("move REFUSED (nothing written): user %s game %s: parts [%s] need making but this profile has no save list yet", u, t, missing); return -1; } }
  char rb[200]; int rn = 0;
  { double tb0 = tm_now(); int gbr = sv_game_back(key, u, t, rb, sizeof rb, &rn); g_t_bk += tm_now() - tb0;
    if (gbr) { look_kv("move/refused", "backup of this profile failed - nothing written"); return -1; } }
  look_kv("move/backup", rb);
  double tmk0 = tm_now();
  if (nmiss) { /* 1.1.45: make the member's parts this profile doesn't have; if one can't be made, undo the ones made and stop */
    static char made[64][32]; int nmade = 0, mfail = 0; char why[200] = "";
    for (int i = 0; i < ns && !mfail; i++) { int f = 0; for (int j = 0; j < nr; j++) if (!strcmp(srcp[i], recv[j])) f = 1; if (f) continue;
      char ps[260]; snprintf(ps, sizeof ps, "%s/%s", src, srcp[i]);
      if (sv_make_part(key, u, t, srcp[i], ps, why, sizeof why)) mfail = 1; else snprintf(made[nmade++], 32, "%s", srcp[i]); }
    if (mfail) { for (int k = 0; k < nmade; k++) { char res[300]; sv_del_part(u, t, made[k], res, sizeof res); }
      snprintf(v, sizeof v, "couldn't make a part (%s) - undone, nothing moved", why); look_kv("move/result", v);
      sv_log("move FAILED: user %s game %s: making a part failed (%s); %d made part(s) removed again, nothing moved", u, t, why, nmade); return -1; }
    snprintf(v, sizeof v, "%d part(s) made", nmade); look_kv("move/made", v);
  }
  g_t_mk += tm_now() - tmk0;
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
    bpop_key("sprog", "Restore point (cloud): %d of %d save parts done, %lld MB, %d failed", done, total, by / 1048576, fails); /* 1.1.76 */
  }
  fclose(td);
  if (pos >= total) snap_finish(key, lab, stamp, done, fails, by);
  if ((long long)time(0) - snap_hb >= 300) { char s2[400]; snap_hb = (long long)time(0); report(key, s2, sizeof s2); write_status(s2); }
}
/* 1.1.43 CLOSE THE GAME (owner 17 Nov 4:03pm: "ok make it" - guests leave games running and every save step refuses while
   a game is open; replaces the 12 Nov rule "the helper never closes a game"). "/_close" closes the game that is open, the
   same close as Options > Close Application (sceSystemServiceKillApp, else sceLncUtilKillApp). Safeguards: the engine only
   sends a look job when the room has no real guest session now or within 15 min (twin/noise blocks don't count); PS Vue is
   never closed; never mid-save - if any save of that game changed in the last 60 s it waits (up to 3 min), else refuses;
   afterwards it checks the game really closed. Everything in save.log. */
static long long game_save_newest(const char *t) { /* newest change to any save image of this game, any profile */
  long long nw = 0; DIR *h = opendir("/user/home"); struct dirent *e;
  while (h && (e = readdir(h))) {
    if (strlen(e->d_name) != 8) continue;
    char d[200]; snprintf(d, sizeof d, "/user/home/%s/savedata/%s", e->d_name, t);
    DIR *g = opendir(d); struct dirent *f;
    while (g && (f = readdir(g))) { if (strncmp(f->d_name, "sdimg_", 6)) continue; char p[300]; struct stat st; snprintf(p, sizeof p, "%s/%s", d, f->d_name); if (!stat(p, &st) && st.st_mtime > nw) nw = st.st_mtime; }
    if (g) closedir(g);
  }
  if (h) closedir(h); return nw;
}
static int close_game(char *res, size_t rn) {
  char t[32]; running_title(t, sizeof t);
  if (!t[0]) { snprintf(res, rn, "no game open"); return 0; }
  if (!strcmp(t, VUE_ID)) { snprintf(res, rn, "PS Vue is open - never closed (owner)"); return -1; }
  int app = sceSystemServiceGetAppIdOfBigApp();
  long long quiet = 0;
  for (int i = 0; i < 12; i++) { /* up to 3 min for a save in progress to finish */
    long long nw = game_save_newest(t), now = (long long)time(0);
    if (now - nw >= 60) { quiet = 1; break; }
    sv_log("close: %s saved %lld s ago - waiting", t, now - nw); sleep(15);
    char t2[32]; running_title(t2, sizeof t2); if (strcmp(t2, t)) { snprintf(res, rn, "%s closed by itself", t); sv_log("close: %s", res); return 0; }
  }
  if (!quiet) { snprintf(res, rn, "%s kept saving for 3 min - not closed", t); sv_log("close REFUSED: %s", res); return -1; }
  static int (*killApp)(uint32_t, int32_t, int32_t, int32_t), (*lncKill)(uint32_t); static int looked;
  if (!looked) { looked = 1; int m = sceKernelLoadStartModule("/system/common/lib/libSceSystemService.sprx", 0, 0, 0, 0, 0);
    if (m > 0) { sceKernelDlsym(m, "sceSystemServiceKillApp", (void **)&killApp); sceKernelDlsym(m, "sceLncUtilKillApp", (void **)&lncKill); } }
  int r1 = -1, r2 = -1;
  if (killApp) r1 = killApp((uint32_t)app, -1, 0, 0);
  for (int i = 0; i < 10; i++) { char t2[32]; running_title(t2, sizeof t2); if (!t2[0]) break; sleep(1); }
  char t3[32]; running_title(t3, sizeof t3);
  if (t3[0] && lncKill) { r2 = lncKill((uint32_t)app); for (int i = 0; i < 10; i++) { running_title(t3, sizeof t3); if (!t3[0]) break; sleep(1); } }
  int ok = !t3[0];
  snprintf(res, rn, "%s %s (kill 0x%08x%s%s)", t, ok ? "closed" : "NOT closed", (unsigned)r1, r2 != -1 ? ", lnc " : "", r2 != -1 ? "tried" : "");
  sv_log("close %s: %s, app 0x%x, SystemServiceKillApp 0x%08x%s, LncUtilKillApp %s", ok ? "done" : "FAILED", t, (unsigned)app, (unsigned)r1, killApp ? "" : " (not found)", r2 == -1 ? "not used" : "used");
  return ok ? 0 : -1;
}
static void look_close(void) { char r[160]; close_game(r, sizeof r); look_kv("close/result", r); }
/* ---- 1.1.44 (owner 17 Nov 4:13pm "u can proceed": make a save part a profile doesn't have yet). Step 1, READ-ONLY:
   "/_sdbinfo.<user>.<game>" writes to save.log how the PS4 itself built that game's saves on that profile - the save-list
   database's layout and the game's rows in it, and each part's param.sfo fields (owner IDs, console fingerprint, blocks, titles)
   - and checks our fingerprint maths (Apollo: HMAC-SHA256 of the console's PSID) against what the PS4 wrote. Nothing written. */
static const unsigned char R2_PSID_KEY[16] = { 0x13, 0xD1, 0xDF, 0x06, 0x75, 0xC9, 0xFD, 0x95, 0x0A, 0x17, 0xE5, 0x64, 0xC2, 0x77, 0x7F, 0x2C };
static void hexs(const unsigned char *b, int n, char *o) { for (int i = 0; i < n; i++) sprintf(o + i * 2, "%02x", b[i]); o[n * 2] = 0; }
static int sfo_find(const unsigned char *b, size_t n, const char *key, uint32_t *off, uint32_t *len, uint16_t *fmt) {
  uint32_t kt, dt, ne;
  if (n < 20 || memcmp(b, "\0PSF", 4)) return -1;
  memcpy(&kt, b + 8, 4); memcpy(&dt, b + 12, 4); memcpy(&ne, b + 16, 4);
  for (uint32_t i = 0; i < ne && 20 + i * 16 + 16 <= n; i++) {
    const unsigned char *x = b + 20 + i * 16; uint16_t ko, f; uint32_t pl, doff;
    memcpy(&ko, x, 2); memcpy(&f, x + 2, 2); memcpy(&pl, x + 4, 4); memcpy(&doff, x + 12, 4);
    if (kt + ko >= n) continue;
    if (!strcmp((const char *)b + kt + ko, key)) { if (dt + doff + pl > n) return -1; *off = dt + doff; *len = pl; if (fmt) *fmt = f; return 0; }
  }
  return -1;
}
static void sv_log_long(const char *pre, const char *txt) { /* save.log lines are read back 300 chars at a time */
  size_t n = strlen(txt); for (size_t i = 0; i < n || i == 0; i += 220) { sv_log("%s%s%.220s", pre, i ? " ...": "", txt + i); if (!n) break; }
}
static void look_sdbinfo(const char *spec) {
  char u[16] = "", t[16] = "", v[200];
  if (sscanf(spec, "/_sdbinfo.%8[0-9a-f].%9[A-Z0-9]", u, t) != 2) { look_kv("dbinfo/error", "bad spec"); return; }
  if (sv_game_open()) { look_kv("dbinfo/refused", "a game is open"); return; }
  sv_rights_up(v, sizeof v);
  uint32_t uid = (uint32_t)strtoul(u, 0, 16);
  unsigned char op[16] = { 0 }, ps[16] = { 0 }, h1[32], h2[32]; char a[80], b2[80];
  sceKernelGetOpenPsId(op);
  int (*psys)(uint8_t *) = 0; int lk = sceKernelLoadStartModule("/system/common/lib/libkernel_sys.sprx", 0, 0, 0, 0, 0);
  if (lk > 0) sceKernelDlsym(lk, "sceKernelGetOpenPsIdForSystem", (void **)&psys);
  int rs = psys ? psys(ps) : -99;
  const mbedtls_md_info_t *mi = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(mi, R2_PSID_KEY, 16, op, 16, h1); mbedtls_md_hmac(mi, R2_PSID_KEY, 16, ps, 16, h2);
  hexs(op, 16, a); hexs(ps, 16, b2); sv_log("dbinfo %s %s: OpenPsId %s, ForSystem %s (rc %d)", u, t, a, b2, rs);
  hexs(h1, 8, a); hexs(h2, 8, b2); sv_log("dbinfo: hmac(OpenPsId) %s.., hmac(ForSystem) %s..", a, b2);
  { long base = prof_slot_base(uid); unsigned char ac[8] = { 0 }; int r = base ? sceRegMgrGetBin(base + 0x500, ac, 8) : -1; hexs(ac, 8, a); sv_log("dbinfo: profile %s account id %s (read %d)", u, a, r); }
  char dp[200]; db_path(u, dp, sizeof dp); sqlite3 *db = 0;
  if (sqlite3_open_v2(dp, &db, SQLITE_OPEN_READONLY, 0) == SQLITE_OK) {
    sqlite3_stmt *q = 0;
    if (sqlite3_prepare_v2(db, "SELECT type||' '||name||': '||ifnull(sql,'') FROM sqlite_master", -1, &q, 0) == SQLITE_OK)
      while (sqlite3_step(q) == SQLITE_ROW) sv_log_long("dbinfo schema: ", (const char *)sqlite3_column_text(q, 0));
    sqlite3_finalize(q);
    int uv = -1, pg = -1;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &q, 0) == SQLITE_OK && sqlite3_step(q) == SQLITE_ROW) uv = sqlite3_column_int(q, 0); sqlite3_finalize(q);
    if (sqlite3_prepare_v2(db, "PRAGMA page_size", -1, &q, 0) == SQLITE_OK && sqlite3_step(q) == SQLITE_ROW) pg = sqlite3_column_int(q, 0); sqlite3_finalize(q);
    sv_log("dbinfo: user_version %d, page_size %d", uv, pg);
    if (sqlite3_prepare_v2(db, "SELECT * FROM savedata WHERE title_id=?1", -1, &q, 0) == SQLITE_OK) {
      sqlite3_bind_text(q, 1, t, -1, SQLITE_STATIC);
      while (sqlite3_step(q) == SQLITE_ROW) {
        static char row[4000]; row[0] = 0; int nc = sqlite3_column_count(q);
        for (int c = 0; c < nc; c++) { size_t l = strlen(row); int ty = sqlite3_column_type(q, c);
          if (ty == SQLITE_BLOB) { char hb[70]; int bl = sqlite3_column_bytes(q, c); hexs(sqlite3_column_blob(q, c), bl > 24 ? 24 : bl, hb); snprintf(row + l, sizeof row - l, "%s=x%s(%d); ", sqlite3_column_name(q, c), hb, bl); }
          else snprintf(row + l, sizeof row - l, "%s=%s; ", sqlite3_column_name(q, c), ty == SQLITE_NULL ? "NULL" : (const char *)sqlite3_column_text(q, c)); }
        sv_log_long("dbinfo row: ", row);
      }
    }
    sqlite3_finalize(q); sqlite3_close(db);
  } else { sv_log("dbinfo: no save list database (%s)", dp); sqlite3_close(db); }
  static char names[16][32]; int np = sv_disk_parts(u, t, names, 16);
  for (int i = 0; i < np && i < 6; i++) {
    char why[160], img[220]; struct stat st; snprintf(img, sizeof img, "/user/home/%s/savedata/%s/sdimg_%s", u, t, names[i]);
    long long isz = stat(img, &st) ? -1 : (long long)st.st_size;
    if (sv_open(u, t, names[i], 1, why, sizeof why)) { sv_log("dbinfo part %s: can't open (%s)", names[i], why); continue; }
    char sp[200]; snprintf(sp, sizeof sp, "%s/sce_sys/param.sfo", SV_MNT); static unsigned char sf[65536]; size_t sn = 0;
    FILE *f = fopen(sp, "rb"); if (f) { sn = fread(sf, 1, sizeof sf, f); fclose(f); }
    sv_close();
    uint32_t off, len; uint16_t fmt; char line[900] = ""; size_t l;
    if (!sfo_find(sf, sn, "ACCOUNT_ID", &off, &len, &fmt) && len <= 16) { hexs(sf + off, len, a); l = strlen(line); snprintf(line + l, sizeof line - l, "ACCOUNT_ID=%s; ", a); }
    if (!sfo_find(sf, sn, "PARAMS", &off, &len, &fmt) && len >= 0x50) { uint32_t pu; memcpy(&pu, sf + off + 4, 4); hexs(sf + off + 8, 8, a);
      l = strlen(line); snprintf(line + l, sizeof line - l, "PARAMS(len %u) user=%08x hmac=%s.. %s t1=%.16s t2=%.16s; ", len, pu, a,
        !memcmp(sf + off + 8, h1, 32) ? "=OpenPsId" : !memcmp(sf + off + 8, h2, 32) ? "=ForSystem" : "=neither", (char *)sf + off + 0x2C, (char *)sf + off + 0x3C); }
    const char *keys[] = { "SAVEDATA_BLOCKS", "SAVEDATA_DIRECTORY", "MAINTITLE", "SUBTITLE", "DETAIL", "TITLE_ID", "CATEGORY", "FORMAT", "SAVEDATA_LIST_PARAM" };
    for (unsigned k = 0; k < sizeof keys / sizeof *keys; k++) if (!sfo_find(sf, sn, keys[k], &off, &len, &fmt)) {
      l = strlen(line);
      if (fmt == 0x0404 && len == 4) { uint32_t x; memcpy(&x, sf + off, 4); snprintf(line + l, sizeof line - l, "%s=%u; ", keys[k], x); }
      else if (fmt == 0x0004 || fmt == 0x0404) { uint64_t x = 0; memcpy(&x, sf + off, len > 8 ? 8 : len); snprintf(line + l, sizeof line - l, "%s=%llu(fmt %04x len %u); ", keys[k], (unsigned long long)x, fmt, len); }
      else snprintf(line + l, sizeof line - l, "%s=%.40s; ", keys[k], (char *)sf + off);
    }
    char pre[80]; snprintf(pre, sizeof pre, "dbinfo part %s (image %lld, sfo %zu): ", names[i], isz, sn); sv_log_long(pre, line);
  }
  sv_rights_back();
  snprintf(v, sizeof v, "%d parts read - details in save.log", np); look_kv("dbinfo/result", v);
}
/* ---- 1.1.45 MAKE A SAVE PART this profile doesn't have (owner 17 Nov). Apollo's recipe (apollo-ps4 sd.c createSave +
   saves.c orbis_SaveMount CREATE2 + sfo.c patch_sfo), checked against saves the PS4 made itself on Kai (/_sdbinfo, 4:17pm):
   1) the size comes from the source's param.sfo SAVEDATA_BLOCKS (PS4 image = blocks x 32 KB, row size_kib = blocks x 32);
   2) a new key from the PS4's key service (/dev/sbl_srv 0x40845303), unlocked (0xc0845302) -> <part>.bin;
   3) the image: sceFsUfsAllocateSaveData + sceFsCreatePfsSaveDataImage -> sdimg_<part>;
   4) opened, the source's sce_sys copied in with param.sfo stamped for THIS profile + console: ACCOUNT_ID = the profile's
      account ID, PARAMS user id, PARAMS console fingerprint = HMAC-SHA256(key, OpenPsId) (matched the PS4's own), title id 2;
   5) its row in the save-list database (same columns as the PS4's own rows; worked on a checked copy, written back whole);
   6) the save-list icon. The game's own files go in afterwards by the normal clean restore. Any failure -> everything undone. */
typedef struct { int blockSize; uint8_t idk[2]; } R2CreateOpt;
static int (*fsUfsAlloc)(int, uint64_t, uint64_t, int), (*fsInitCreate)(R2CreateOpt *), (*fsCreateImg)(R2CreateOpt *, const char *, int, uint64_t, uint8_t *);
static int sv_make_lib(void) {
  if (fsUfsAlloc && fsInitCreate && fsCreateImg) return 0;
  int m = sceKernelLoadStartModule("/system/priv/lib/libSceFsInternalForVsh.sprx", 0, 0, 0, 0, 0); if (m < 0) return m;
  sceKernelDlsym(m, "sceFsUfsAllocateSaveData", (void **)&fsUfsAlloc); sceKernelDlsym(m, "sceFsInitCreatePfsSaveDataOpt", (void **)&fsInitCreate);
  sceKernelDlsym(m, "sceFsCreatePfsSaveDataImage", (void **)&fsCreateImg);
  return fsUfsAlloc && fsInitCreate && fsCreateImg ? 0 : -1;
}
static int sv_new_key(uint8_t enc[0x60], uint8_t dec[0x20]) {
  uint8_t a[0x90], b[0x90]; memset(a, 0, sizeof a); memset(b, 0, sizeof b);
  int fd = open("/dev/sbl_srv", O_RDWR); if (fd < 0) return -1;
  if (ioctl(fd, 0x40845303, a)) { close(fd); return -2; }
  memcpy(enc, a, 0x60); memcpy(b, a, 0x60);
  int r = ioctl(fd, 0xc0845302, b); close(fd); if (r) return -3;
  memcpy(dec, b + 0x60, 0x20); memset(b, 0, sizeof b); return 0;
}
static int sv_db_add(const char *u, const char *t, const char *nm, const char *mt, const char *st, const char *dt, long long blocks, long long acct, uint32_t uid, char *why, size_t wn) {
  char dp[200], bak[200], work[] = "/data/round2/db/work.db";
  db_path(u, dp, sizeof dp); mkdir("/data/round2/db", 0777);
  snprintf(bak, sizeof bak, "/data/round2/db/%s-%lld.db", u, (long long)time(0));
  if (file_copy(dp, bak) || file_copy(dp, work)) { snprintf(why, wn, "can't copy the save list (errno %d)", errno); return -1; }
  sqlite3 *db = 0; if (sqlite3_open_v2(work, &db, SQLITE_OPEN_READWRITE, 0) != SQLITE_OK) { snprintf(why, wn, "save list won't open"); sqlite3_close(db); return -1; }
  sqlite3_exec(db, "PRAGMA journal_mode=OFF", 0, 0, 0);
  int before = db_count(db, t, nm);
  char mtime[40]; time_t now = time(0); struct tm tm; gmtime_r(&now, &tm); strftime(mtime, sizeof mtime, "%Y-%m-%dT%H:%M:%S.00Z", &tm);
  sqlite3_stmt *q = 0; int rc = sqlite3_prepare_v2(db, "INSERT INTO savedata(title_id, dir_name, main_title, sub_title, detail, tmp_dir_name, is_broken, user_param, blocks, free_blocks, size_kib, mtime, fake_broken, account_id, user_id, faked_owner, cloud_icon_url, cloud_revision, game_title_id) "
    "VALUES (?1, ?2, ?3, ?4, ?5, '', 0, 0, ?6, ?6, ?7, ?8, 0, ?9, ?10, 0, '', 0, ?1)", -1, &q, 0);
  if (rc == SQLITE_OK) {
    sqlite3_bind_text(q, 1, t, -1, SQLITE_STATIC); sqlite3_bind_text(q, 2, nm, -1, SQLITE_STATIC); sqlite3_bind_text(q, 3, mt, -1, SQLITE_STATIC);
    sqlite3_bind_text(q, 4, st, -1, SQLITE_STATIC); sqlite3_bind_text(q, 5, dt, -1, SQLITE_STATIC); sqlite3_bind_int64(q, 6, blocks); sqlite3_bind_int64(q, 7, blocks * 32);
    sqlite3_bind_text(q, 8, mtime, -1, SQLITE_STATIC); sqlite3_bind_int64(q, 9, acct); sqlite3_bind_int64(q, 10, (long long)uid);
    rc = sqlite3_step(q) == SQLITE_DONE ? SQLITE_OK : -1;
  }
  sqlite3_finalize(q);
  int after = db_count(db, t, nm);
  char ok[16] = ""; sqlite3_stmt *ic = 0;
  if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &ic, 0) == SQLITE_OK && sqlite3_step(ic) == SQLITE_ROW) snprintf(ok, sizeof ok, "%s", (const char *)sqlite3_column_text(ic, 0));
  sqlite3_finalize(ic); sqlite3_close(db);
  if (rc != SQLITE_OK || before != 0 || after != 1 || strcmp(ok, "ok")) { snprintf(why, wn, "save list row not added (rc %d, %d->%d, check %s)", rc, before, after, ok); unlink(work); return -1; }
  if (file_copy(work, dp)) { snprintf(why, wn, "writing the save list back failed (errno %d)", errno); return -1; }
  unlink(work); return 0;
}
static int sv_make_part_do(const char *key, const char *u, const char *t, const char *nm, const char *src, char *why, size_t wn) {
  double mt0 = tm_now(), mt1 = 0, mt2 = 0, mt3 = 0, mt4 = 0; /* 1.1.80: which step of making a part is slow? */
  char img[220], kp[220], dp[200], prep[260]; struct stat st;
  snprintf(img, sizeof img, "/user/home/%s/savedata/%s/sdimg_%s", u, t, nm); snprintf(kp, sizeof kp, "/user/home/%s/savedata/%s/%s.bin", u, t, nm);
  db_path(u, dp, sizeof dp); snprintf(prep, sizeof prep, "/data/round2/prep/%s/%s/%s", u, t, nm);
  if (!strcmp(t, VUE_ID) || (r2_protected(t) && (uint32_t)strtoul(u, 0, 16) == r2_main_user())) { snprintf(why, wn, "not allowed for this game/profile"); return -1; }
  if (!stat(img, &st) || !stat(kp, &st)) { snprintf(why, wn, "part already exists"); return -1; }
  if (stat(dp, &st)) { snprintf(why, wn, "profile has no save list yet"); return -1; }
  if (!sv_buf) { void *b = mmap(0, SV_PIECE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (b == MAP_FAILED) { snprintf(why, wn, "no memory"); return -1; } sv_buf = b; }
  /* the source's sce_sys files (labels), from the cloud */
  char mk[300]; snprintf(mk, sizeof mk, "%s/manifest.txt", src);
  unsigned char *man = 0; size_t ml = 0; char gw[130] = "";
  if (sv_get(key, mk, &man, &ml, gw, sizeof gw) || !ml) { free(man); snprintf(why, wn, "source not found (%s)", gw); return -1; }
  static char mt[16384]; size_t mc = ml < sizeof mt - 1 ? ml : sizeof mt - 1; memcpy(mt, man, mc); mt[mc] = 0; free(man);
  static char sysn[16][64]; static unsigned char *sysd[16]; static size_t sysl[16]; int ns = 0, sfoi = -1;
  { /* 1.1.81: all label files in one request (fallback: one by one) */
    static char lk[16][400]; const char *kp2[16]; int nk = 0;
    for (char *ln = mt, *nx; ln && *ln && nk < 16; ln = nx) {
      nx = strchr(ln, '\n'); if (nx) *nx++ = 0;
      char *t1 = strchr(ln, '\t'); if (!t1) continue; *t1 = 0;
      if (strncmp(ln, "sce_sys/", 8) || strchr(ln + 8, '/') || strlen(ln) > 60) continue;
      snprintf(lk[nk], sizeof lk[nk], "%s/f/%s~0", src, ln); snprintf(sysn[nk], 64, "%s", ln + 8); kp2[nk] = lk[nk]; nk++;
    }
    int gr = sv_getm(key, kp2, nk, sysd, sysl, gw, sizeof gw);
    if (gr == -2) { gr = 0; for (int i = 0; i < nk && !gr; i++) if (sv_get(key, lk[i], &sysd[i], &sysl[i], gw, sizeof gw)) gr = -1; }
    if (!gr) for (int i = 0; i < nk; i++) if (!sysd[i]) { gr = -1; snprintf(gw, sizeof gw, "%s missing in the cloud", sysn[i]); }
    if (gr) { for (int i = 0; i < nk; i++) { free(sysd[i]); sysd[i] = 0; } snprintf(why, wn, "labels: %s", gw); return -1; }
    ns = nk; for (int i = 0; i < ns; i++) if (!strcmp(sysn[i], "param.sfo")) sfoi = i;
  }
  #define MK_FAIL(...) do { snprintf(why, wn, __VA_ARGS__); for (int i_ = 0; i_ < ns; i_++) free(sysd[i_]); sv_log("make part FAILED: user %s game %s part %s: %s", u, t, nm, why); return -1; } while (0)
  if (sfoi < 0) MK_FAIL("source has no param.sfo");
  unsigned char *sf = sysd[sfoi]; size_t sn = sysl[sfoi]; uint32_t off, len; uint16_t fmt;
  long long blocks = 0; if (!sfo_find(sf, sn, "SAVEDATA_BLOCKS", &off, &len, &fmt) && len == 8) memcpy(&blocks, sf + off, 8);
  if (blocks < 1 || blocks > 65536) MK_FAIL("odd size in the source (%lld blocks)", blocks);
  char mtl[130] = "", stl[130] = "", dtl[1030] = "";
  if (!sfo_find(sf, sn, "MAINTITLE", &off, &len, &fmt)) snprintf(mtl, sizeof mtl, "%.*s", (int)len, (char *)sf + off);
  if (!sfo_find(sf, sn, "SUBTITLE", &off, &len, &fmt)) snprintf(stl, sizeof stl, "%.*s", (int)len, (char *)sf + off);
  if (!sfo_find(sf, sn, "DETAIL", &off, &len, &fmt)) snprintf(dtl, sizeof dtl, "%.*s", (int)len, (char *)sf + off);
  /* stamp param.sfo for this profile + console */
  uint32_t uid = (uint32_t)strtoul(u, 0, 16); long base = prof_slot_base(uid); unsigned char ac[8] = { 0 };
  if (!base || sceRegMgrGetBin(base + 0x500, ac, 8)) MK_FAIL("can't read this profile's account ID");
  long long acct; memcpy(&acct, ac, 8);
  unsigned char op[16] = { 0 }, hm[32]; if (sceKernelGetOpenPsId(op)) MK_FAIL("can't read the console ID");
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), R2_PSID_KEY, 16, op, 16, hm);
  if (sfo_find(sf, sn, "ACCOUNT_ID", &off, &len, &fmt) || len != 8) MK_FAIL("param.sfo has no ACCOUNT_ID"); memcpy(sf + off, ac, 8);
  if (sfo_find(sf, sn, "PARAMS", &off, &len, &fmt) || len < 0x50) MK_FAIL("param.sfo has no PARAMS");
  memcpy(sf + off + 4, &uid, 4); memcpy(sf + off + 8, hm, 32); memcpy(sf + off + 0x3C, sf + off + 0x2C, 0x10);
  /* key + image */
  mt1 = tm_now(); r2_timing("make part: labels from the cloud", mt1 - mt0);
  if (sv_lib() || sv_make_lib()) MK_FAIL("save-making library missing");
  char dir[200]; snprintf(dir, sizeof dir, "/user/home/%s/savedata/%s/x", u, t); mkdirs_for(dir);
  uint8_t enc[0x60], dec[0x20]; int kr = sv_new_key(enc, dec); if (kr) MK_FAIL("new key from the key service failed (%d)", kr);
  mt2 = tm_now(); r2_timing("make part: new key", mt2 - mt1);
  int fd = open(kp, O_CREAT | O_TRUNC | O_WRONLY, 0777); if (fd < 0) MK_FAIL("can't write the key (errno %d)", errno);
  int wr = (int)write(fd, enc, 0x60); fsync(fd); close(fd); if (wr != 0x60) { unlink(kp); MK_FAIL("key write %d", wr); }
  uint64_t vsz = (uint64_t)blocks << 15;
  fd = open(img, O_CREAT | O_TRUNC | O_WRONLY, 0777); if (fd < 0) { unlink(kp); MK_FAIL("can't create the image (errno %d)", errno); }
  int ar = fsUfsAlloc(fd, vsz, 0, 0); close(fd);
  R2CreateOpt co; memset(&co, 0, sizeof co); int ir = ar < 0 ? -1 : fsInitCreate(&co);
  int cr = ar < 0 || ir < 0 ? -1 : fsCreateImg(&co, img, 0, vsz, dec);
  memset(dec, 0, sizeof dec);
  if (ar < 0 || ir < 0 || cr < 0) { unlink(img); unlink(kp); MK_FAIL("image not made (allocate 0x%08x, init 0x%08x, create 0x%08x)", (unsigned)ar, (unsigned)ir, (unsigned)cr); }
  fd = open(img, O_RDONLY); if (fd >= 0) { fsync(fd); close(fd); }
  mt3 = tm_now(); r2_timing("make part: make the image", mt3 - mt2);
  /* labels in */
  char ow[160] = "";
  if (sv_open(u, t, nm, 0, ow, sizeof ow)) { unlink(img); unlink(kp); MK_FAIL("new image won't open (%s)", ow); }
  char sd[200]; snprintf(sd, sizeof sd, "%s/sce_sys", SV_MNT); mkdir(sd, 0777);
  int wf = 0;
  for (int i = 0; i < ns; i++) { char p[300]; snprintf(p, sizeof p, "%s/%s", sd, sysn[i]); int f = open(p, O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (f < 0 || write(f, sysd[i], sysl[i]) != (ssize_t)sysl[i]) wf++; if (f >= 0) { fsync(f); close(f); } }
  int clr = sv_close();
  if (wf) { unlink(img); unlink(kp); MK_FAIL("%d label file(s) not written", wf); }
  /* its row in the save list, then the icon */
  mt4 = tm_now(); r2_timing("make part: open + labels in", mt4 - mt3);
  char dw[200] = "";
  if (sv_db_add(u, t, nm, mtl, stl, dtl, blocks, acct, uid, dw, sizeof dw)) { unlink(img); unlink(kp); MK_FAIL("%s", dw); }
  for (int i = 0; i < ns; i++) if (!strcmp(sysn[i], "icon0.png")) { char ip[260]; snprintf(ip, sizeof ip, "/user/home/%s/savedata_meta/user/%s/%s_icon0.png", u, t, nm); mkdirs_for(ip);
    int f = open(ip, O_CREAT | O_TRUNC | O_WRONLY, 0666); if (f >= 0) { if (write(f, sysd[i], sysl[i]) < 0) {} fsync(f); close(f); } }
  for (int i = 0; i < ns; i++) free(sysd[i]);
  { double mt5 = tm_now(); r2_timing("make part: save list + icon", mt5 - mt4);
    sv_log("make part timing: %s %s %s: labels from cloud %.2f s, key %.2f s, image %.2f s (%lld blocks), open+labels %.2f s, save list %.2f s", u, t, nm, mt1 - mt0, mt2 - mt1, mt3 - mt2, blocks, mt4 - mt3, mt5 - mt4); }
  sv_log("make part done: user %s game %s part %s: %lld blocks (%llu bytes), %d label files, account %016llx, close %d", u, t, nm, blocks, (unsigned long long)vsz, ns, (unsigned long long)acct, clr);
  #undef MK_FAIL
  (void)prep; return 0;
}
static int sv_make_part(const char *key, const char *u, const char *t, const char *nm, const char *src, char *why, size_t wn) {
  char rv[200]; sv_rights_up(rv, sizeof rv);   /* the key service + system files need the raised rights, like backup/restore */
  int r = sv_make_part_do(key, u, t, nm, src, why, wn);
  sv_rights_back(); return r;
}
/* ---- 1.1.46 AVATARS (owner 17 Nov: members choose a profile picture; Lapy's Avatar Changer / .xavatar use the same files
   the PS4 keeps per profile in /system_data/priv/cache/profile/0x<USER ID>/: avatar.png + avatar64/128/260/440.dds,
   picture.png + picture64/128/260/440.dds, online.json; version.json stays). The PS4 reads them at sign-in, so they're
   changed while the profile is signed OUT. "/_avinfo.<user>" lists the folder + logs online.json; "/_avcopy.<from>.<to>"
   copies the picture files (not online.json - first test); "/_avreset.<user>" puts the default picture back. Before any
   change the target's files are kept in /data/round2/avbak/<user>-<time>/. Never the main profile as a target. */
static int user_signed_in(uint32_t uid) { UserIdList l; memset(&l, 0xff, sizeof l); sceUserServiceGetLoginUserIdList(&l);
  for (int i = 0; i < 4; i++) if ((uint32_t)l.userId[i] == uid) return 1; return 0; }
static int av_name(const char *n) { return (!strncmp(n, "avatar", 6) || !strncmp(n, "picture", 7)) && (strstr(n, ".png") || strstr(n, ".dds")); }
static void av_dir(uint32_t uid, char *d, size_t n) { snprintf(d, n, "/system_data/priv/cache/profile/0x%08X", uid); }
static int av_keep(uint32_t uid, const char *d) { /* copy the folder's current files to /data/round2/avbak/<uid>-<time>/ */
  char b[200]; snprintf(b, sizeof b, "/data/round2/avbak/%08x-%lld/x", uid, (long long)time(0)); mkdirs_for(b); b[strlen(b) - 2] = 0;
  DIR *x = opendir(d); struct dirent *e; int n = 0;
  while (x && (e = readdir(x))) { if (e->d_name[0] == '.') continue; char a[300], c[300]; snprintf(a, sizeof a, "%s/%s", d, e->d_name); snprintf(c, sizeof c, "%s/%s", b, e->d_name); if (!file_copy(a, c)) n++; }
  if (x) closedir(x); sv_log("avatar: kept %d file(s) of %08x in %s", n, uid, b); return n;
}
static void av_log_json(uint32_t uid) {
  char d[120], p[200]; av_dir(uid, d, sizeof d); snprintf(p, sizeof p, "%s/online.json", d);
  FILE *f = fopen(p, "r"); if (!f) { sv_log("avatar: %08x has no online.json", uid); return; }
  static char b[2048]; size_t r = fread(b, 1, sizeof b - 1, f); fclose(f); b[r] = 0; for (char *c = b; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
  char pre[60]; snprintf(pre, sizeof pre, "avatar: %08x online.json: ", uid); sv_log_long(pre, b);
}
static void look_avinfo(const char *spec) {
  char u[16] = ""; if (sscanf(spec, "/_avinfo.%8[0-9a-f]", u) != 1) { look_kv("avatar/error", "bad spec"); return; }
  uint32_t uid = (uint32_t)strtoul(u, 0, 16); char d[120], rv[200]; av_dir(uid, d, sizeof d);
  sv_rights_up(rv, sizeof rv); look_dir(d, 0, 0); av_log_json(uid); sv_rights_back();
  look_kv("avatar/signed-in", user_signed_in(uid) ? "yes" : "no");
}
static void look_avcopy(const char *spec, int reset) {
  char a[16] = "", b[16] = "", v[160];
  if (reset ? sscanf(spec, "/_avreset.%8[0-9a-f]", b) != 1 : sscanf(spec, "/_avcopy.%8[0-9a-f].%8[0-9a-f]", a, b) != 2) { look_kv("avatar/error", "bad spec"); return; }
  uint32_t from = (uint32_t)strtoul(a, 0, 16), to = (uint32_t)strtoul(b, 0, 16);
  if (to == r2_main_user()) { look_kv("avatar/refused", "not the main profile"); return; }
  if (!prof_slot_base(to)) { look_kv("avatar/refused", "no such profile"); return; }
  if (user_signed_in(to)) { look_kv("avatar/refused", "that profile is signed in - sign it out first"); return; }
  char fd[120], td[120], rv[200]; av_dir(from, fd, sizeof fd); av_dir(to, td, sizeof td);
  sv_rights_up(rv, sizeof rv);
  mkdir(td, 0777); av_keep(to, td);
  int rm = 0, cp = 0, bad = 0; DIR *x = opendir(td); struct dirent *e;
  while (x && (e = readdir(x))) if (av_name(e->d_name) || (reset && !strcmp(e->d_name, "online.json"))) { char p[300]; snprintf(p, sizeof p, "%s/%s", td, e->d_name); if (!unlink(p)) rm++; }
  if (x) closedir(x);
  if (!reset) { x = opendir(fd);
    while (x && (e = readdir(x))) if (av_name(e->d_name)) { char p[300], q[300]; snprintf(p, sizeof p, "%s/%s", fd, e->d_name); snprintf(q, sizeof q, "%s/%s", td, e->d_name); if (file_copy(p, q)) bad++; else cp++; }
    if (x) closedir(x); av_log_json(from); }
  sv_rights_back();
  if (reset) snprintf(v, sizeof v, "default picture back: %d file(s) removed", rm);
  else snprintf(v, sizeof v, "%d picture file(s) copied, %d failed, %d old removed - sign in to see it", cp, bad, rm);
  sv_log("avatar %s: %s -> %s: %s", reset ? "reset" : "copy", reset ? "-" : a, b, v);
  look_kv("avatar/result", v);
}
/* ---- 1.1.47 DELETE A PROFILE (owner 17 Nov: each PS4 keeps main + User1-4, recycled; extra profiles guests create are backed
   up / parked under the booking, then deleted). "/_udel.<user>" = sceUserServiceDestroyUser, the PS4's own delete (Settings >
   User Management; also what ps5upload uses). Refused: the main profile, a signed-in profile (checked again right before), a
   game open, any save part still on the profile (back it up / park it first - nothing is ever deleted unsaved). Afterwards
   checks the profile, its home folder and its save list are gone, and removes the picture folder the PS4 leaves behind. */
static void look_udel(const char *spec) {
  char u[16] = "", v[200]; if (sscanf(spec, "/_udel.%8[0-9a-f]", u) != 1 || strlen(spec) != 15) { look_kv("udel/error", "bad spec"); return; }
  uint32_t uid = (uint32_t)strtoul(u, 0, 16);
  if (uid == r2_main_user()) { look_kv("udel/refused", "the main profile is never deleted"); return; }
  if (!prof_slot_base(uid)) { look_kv("udel/refused", "no such profile"); return; }
  if (sv_game_open()) { look_kv("udel/refused", "a game is open"); return; }
  { int parts = 0; char sd[120]; snprintf(sd, sizeof sd, "/user/home/%s/savedata", u); DIR *g = opendir(sd); struct dirent *f; static char nm[64][32];
    while (g && (f = readdir(g))) if (strlen(f->d_name) == 9) parts += sv_disk_parts(u, f->d_name, nm, 64);
    if (g) closedir(g);
    if (parts) { snprintf(v, sizeof v, "%d save part(s) still on it - back up / park first", parts); look_kv("udel/refused", v); return; } }
  if (user_signed_in(uid)) { look_kv("udel/refused", "signed in - sign it out first"); return; }
  static int (*destroy)(int); static int looked;
  if (!looked) { looked = 1; int m = sceKernelLoadStartModule("/system/common/lib/libSceUserService.sprx", 0, 0, 0, 0, 0); if (m > 0) sceKernelDlsym(m, "sceUserServiceDestroyUser", (void **)&destroy); }
  if (!destroy) { look_kv("udel/error", "delete function not found"); return; }
  char name[64] = ""; sceUserServiceGetUserName((int)uid, name, sizeof name);
  char rv[200]; sv_rights_up(rv, sizeof rv);
  int r = destroy((int)uid);
  sleep(2);
  struct stat st; char h[120], sd2[120], av[120]; snprintf(h, sizeof h, "/user/home/%s", u); snprintf(sd2, sizeof sd2, "/system_data/savedata/%s", u); av_dir(uid, av, sizeof av);
  int still = prof_slot_base(uid) != 0, home = !stat(h, &st), sl = !stat(sd2, &st), avl = 0;
  if (r == 0 && !still && !stat(av, &st)) { sv_wipe_data(av, "x"); avl = rmdir(av) == 0; }
  sv_rights_back();
  snprintf(v, sizeof v, "%s '%s' (rc 0x%08x): profile %s, home %s, save list %s%s", r == 0 && !still ? "deleted" : "NOT deleted", name, (unsigned)r,
    still ? "STILL THERE" : "gone", home ? "still there" : "gone", sl ? "still there" : "gone", avl ? ", picture folder removed" : "");
  sv_log("profile delete: %s %s", u, v); look_kv("udel/result", v);
}
/* ---- 1.1.48 PROFILE ORDER (owner 17 Nov 7:24pm: Kai showed Kai, Player 3, Aliff, Player 4, Karen). The profile-select
   screen lists the main profile first, then by setting 0x0900 of each profile slot, highest first = most recently signed in
   (Kai: Player 3 483, Kai 481, Aliff 474, Player 4 471, Karen 435). "/_uorder.<user>-<user>-..." gives the listed profiles
   the SAME numbers they hold now, re-dealt in the wanted order (biggest to the first) - so the PS4's own counting carries on
   and a member who signs in still jumps to the front. Profiles not listed keep theirs. Old values in save.log. */
static void look_uorder(const char *spec) {
  /* 1.1.49: ids separated by "-" (the engine keeps only letters, digits and . / | ; - in a look path) */
  char lst[200] = ""; if (sscanf(spec, "/_uorder.%199[0-9a-f-]", lst) != 1) { look_kv("order/error", "bad spec"); return; }
  uint32_t ids[16]; long bases[16]; int vals[16], n = 0;
  for (char *t = lst; *t && n < 16; ) {
    char *e = strchr(t, '-'); size_t len = e ? (size_t)(e - t) : strlen(t);
    if (len != 8) { look_kv("order/error", "bad user id"); return; }
    char one[9]; memcpy(one, t, 8); one[8] = 0; t += len + (e ? 1 : 0);
    ids[n] = (uint32_t)strtoul(one, 0, 16); bases[n] = prof_slot_base(ids[n]);
    if (!bases[n]) { look_kv("order/error", "no such profile"); return; }
    if (sceRegMgrGetInt(bases[n] + 0x900, &vals[n])) { look_kv("order/error", "can't read the order number"); return; }
    n++;
  }
  if (n < 2) { look_kv("order/error", "list at least 2 profiles"); return; }
  int sorted[16]; memcpy(sorted, vals, sizeof(int) * n);
  for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (sorted[j] > sorted[i]) { int x = sorted[i]; sorted[i] = sorted[j]; sorted[j] = x; }
  char old[300] = "", nw[300] = ""; int bad = 0;
  for (int i = 0; i < n; i++) {
    size_t l = strlen(old); snprintf(old + l, sizeof old - l, "%s%08x=%d", i ? " " : "", ids[i], vals[i]);
    if (sceRegMgrSetInt(bases[i] + 0x900, sorted[i])) bad++;
    l = strlen(nw); snprintf(nw + l, sizeof nw - l, "%s%08x=%d", i ? " " : "", ids[i], sorted[i]);
  }
  sv_log("profile order: before %s; after %s; %d write(s) failed", old, nw, bad);
  char v[120]; snprintf(v, sizeof v, "%d profiles re-ordered, %d failed - check the profile screen", n, bad); look_kv("order/result", v);
}
/* ---- 1.1.51 INTERNAL FIRST (owner 17 Nov 7:43pm, critical: "save to internal first ... batch save them in the background,
   it takes less effort and has less failures" - agreed earlier, then missed). A part is copied from the opened save to the
   PS4's own storage /data/round2/prep/<user>/<game>/<part>/ with a fingerprint (SHA-256) per file in manifest.txt
   ("path<TAB>size<TAB>sha256"); the copy is checked (sizes) before anything is removed; the part is then queued in
   /data/round2/prep/pending.txt for the later background cloud batch (which can skip files whose fingerprint is already up). */
static int lc_walk(const char *src, const char *dst, const char *rel, FILE *man, int depth, int *nf, unsigned long long *nb) {
  DIR *d = opendir(src); if (!d) return -1; struct dirent *e; int bad = 0;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char sp[512], dp[512], r[400]; snprintf(sp, sizeof sp, "%s/%s", src, e->d_name); snprintf(dp, sizeof dp, "%s/%s", dst, e->d_name);
    snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
    struct stat st; if (lstat(sp, &st)) { bad++; continue; }
    if (S_ISDIR(st.st_mode)) { if (depth < 8) { mkdir(dp, 0777); bad += lc_walk(sp, dp, r, man, depth + 1, nf, nb) < 0; } continue; }
    if (!S_ISREG(st.st_mode)) continue;
    int in = open(sp, O_RDONLY), out = open(dp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (in < 0 || out < 0) { if (in >= 0) close(in); if (out >= 0) close(out); bad++; continue; }
    mbedtls_sha256_context h; mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h, 0);
    unsigned long long got = 0; ssize_t x; int werr = 0;
    while ((x = read(in, sv_buf, SV_PIECE)) > 0) { if (write(out, sv_buf, (size_t)x) != x) { werr = 1; break; } mbedtls_sha256_update(&h, sv_buf, (size_t)x); got += (unsigned long long)x; }
    unsigned char hs[32]; char hx[65]; mbedtls_sha256_finish(&h, hs); mbedtls_sha256_free(&h); hexs(hs, 32, hx);
    fsync(out); close(out); close(in);
    struct stat ds; if (x < 0 || werr || stat(dp, &ds) || (unsigned long long)ds.st_size != (unsigned long long)st.st_size || got != (unsigned long long)st.st_size) { bad++; continue; }
    fprintf(man, "%s\t%llu\t%s\n", r, got, hx); (*nf)++; *nb += got;
  }
  closedir(d); return bad ? -1 : 0;
}
static int sv_local_copy(const char *u, const char *t, const char *nm, int *nf, unsigned long long *nb, char *why, size_t wn) {
  char dst[260], rv[200]; snprintf(dst, sizeof dst, "/data/round2/prep/%s/%s/%s", u, t, nm);
  if (!sv_buf) { void *b = mmap(0, SV_PIECE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (b == MAP_FAILED) { snprintf(why, wn, "no memory"); return -1; } sv_buf = b; }
  /* 1.1.78 (owner 19 Nov 5:30pm: "unchanged files don't upload?"): keep the cloud batch's notes (uploaded.txt = the manifest
     last sent, cloud.txt = where) when the copy is redone - wiping them made every unchanged save look new (Kaeden: 16 parts
     sent again after a clean slate). */
  size_t ul = 0, cl2 = 0; char up_p[300], cl_p[300]; snprintf(up_p, sizeof up_p, "%s/uploaded.txt", dst); snprintf(cl_p, sizeof cl_p, "%s/cloud.txt", dst);
  char *upk = read_all(up_p, &ul, 256 * 1024), *clk = read_all(cl_p, &cl2, 4096);
  sv_wipe_data(dst, "x"); char mk[300]; snprintf(mk, sizeof mk, "%s/x", dst); mkdirs_for(mk);
  if (upk) { FILE *f = fopen(up_p, "w"); if (f) { fwrite(upk, 1, ul, f); fclose(f); } free(upk); }
  if (clk) { FILE *f = fopen(cl_p, "w"); if (f) { fwrite(clk, 1, cl2, f); fclose(f); } free(clk); }
  sv_rights_up(rv, sizeof rv);
  if (sv_open(u, t, nm, 1, why, wn)) { sv_rights_back(); return -1; }
  char mp[300]; snprintf(mp, sizeof mp, "%s/manifest.txt", dst); FILE *man = fopen(mp, "w");
  int r = man ? lc_walk(SV_MNT, dst, "", man, 0, nf, nb) : -1;
  if (man) { fflush(man); fsync(fileno(man)); fclose(man); }
  sv_close(); sv_rights_back();
  if (r || !*nf) { snprintf(why, wn, "internal copy incomplete (%d files)", *nf); return -1; }
  /* 1.1.84 STAGE 2: if a member owns this profile (/_memtag or /_memload), the cloud batch sends this part to THEIR folder.
     GT Sport (protected titles) is never a member's save (owner 19 Nov 7:47pm: a free-for-all house save). */
  char mt[64] = ""; { char op[120]; snprintf(op, sizeof op, "/data/round2/prep/owner_%s.txt", u); FILE *of = fopen(op, "r"); if (of) { if (!fgets(mt, sizeof mt, of)) mt[0] = 0; fclose(of); mt[strcspn(mt, "\r\n ")] = 0; } }
  if (r2_protected(t)) mt[0] = 0;
  FILE *q = fopen("/data/round2/prep/pending.txt", "a"); if (q) { if (mt[0]) fprintf(q, "%s %s %s %lld m=%s\n", u, t, nm, (long long)time(0), mt); else fprintf(q, "%s %s %s %lld\n", u, t, nm, (long long)time(0)); fclose(q); }
  { char ip[200], sp2[200]; struct stat ss; snprintf(ip, sizeof ip, "/user/home/%s/savedata/%s/sdimg_%s", u, t, nm); snprintf(sp2, sizeof sp2, "%s/sdimg.mt", dst);
    if (!stat(ip, &ss)) { FILE *f = fopen(sp2, "w"); if (f) { fprintf(f, "%lld %lld\n", (long long)ss.st_mtime, (long long)ss.st_size); fclose(f); } } } /* 1.1.102: what the save looked like when copied */
  return 0;
}
/* ---- 1.1.102 SAVE WHEN THE GAME CLOSES (owner 20 Nov 3:15am: "every 15 min the game checks the saves fingerprint to the internal
   stores fingerprint to see if a transfer needs to happen after game is closed ... after 1 fingerprint sees a difference the
   fingerprint can stop and just prepare to move after close"). While a game runs, every 15 min: is any of its save files
   (/user/home/<user>/savedata/<game>/sdimg_<part>: time + size) different from when we last copied it (prep/.../sdimg.mt)? The
   first difference = "changed", no more checks. When the game closes (not during a clean slate - that copies everything
   anyway): each changed part -> internal copy (sv_local_copy: manifest + fingerprints, queued for the cloud batch, which sends
   it to the member's crew folder if the profile is linked, only files that changed). Never PS Vue, protected house saves
   (GT Sport on Max), homebrew, the Round2 tile. Saves are never read while the game is open (it may be half-way through
   writing). */
static int sw_part_changed(const char *u, const char *t, const char *nm) {
  char ip[200], mp[200], b[64] = ""; struct stat ss; snprintf(ip, sizeof ip, "/user/home/%s/savedata/%s/sdimg_%s", u, t, nm); if (stat(ip, &ss)) return 0;
  snprintf(mp, sizeof mp, "/data/round2/prep/%s/%s/%s/sdimg.mt", u, t, nm); FILE *f = fopen(mp, "r"); if (!f) return 1;
  if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); long long m = 0, z = 0; sscanf(b, "%lld %lld", &m, &z);
  return m != (long long)ss.st_mtime || z != (long long)ss.st_size;
}
/* calls fn for every (user, part) of game t that changed; returns how many */
static int sw_scan(const char *t, int copy, const char *key, int *ok, int *bad, char *who, size_t wn) {
  RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L); int n = 0; if (who) who[0] = 0;
  for (int i = 0; i < 16; i++) { if (L.userId[i] == -1 || !L.userId[i]) continue; char u[16], d[160]; snprintf(u, sizeof u, "%08x", (uint32_t)L.userId[i]);
    snprintf(d, sizeof d, "/user/home/%s/savedata/%s", u, t); DIR *x = opendir(d); if (!x) continue; struct dirent *e; char parts[16][48]; int np = 0;
    while ((e = readdir(x)) && np < 16) if (!strncmp(e->d_name, "sdimg_", 6) && strlen(e->d_name) < 54) snprintf(parts[np++], 48, "%s", e->d_name + 6);
    closedir(x);
    int mine = 0;
    for (int k = 0; k < np; k++) if (sw_part_changed(u, t, parts[k])) { n++; mine++;
      if (copy) { int nf = 0; unsigned long long nb = 0; char why[200] = ""; if (sv_local_copy(u, t, parts[k], &nf, &nb, why, sizeof why)) { (*bad)++; sv_log("save watch: %s %s %s NOT copied: %s", u, t, parts[k], why); } else { (*ok)++; sv_log("save watch: %s %s %s copied to the PS4 (%d files, %llu bytes) - queued for the cloud", u, t, parts[k], nf, nb); } } }
    if (mine && who) { char nm[40] = ""; sceUserServiceGetUserName(L.userId[i], nm, sizeof nm); size_t l = strlen(who); if (l < wn - 1) snprintf(who + l, wn - l, "%s%s", l ? ", " : "", nm); }
  }
  return n;
}
static void pname(const char *u, char *o, size_t n) { o[0] = 0; sceUserServiceGetUserName((int32_t)strtoul(u, 0, 16), o, n); if (!o[0]) snprintf(o, n, "profile %s", u); }
/* 1.1.108 (owner 20 Nov 3:48am: a controller switched on during a load brings up the profile picker – "would clash"): every 2 s
   while a member load runs (clean slate kind "prep", or players waiting in the queue), anyone who signs in gets a PS4 message
   "Hang on – saves are still loading (player k of n)...". The load itself carries on (it's safe with profiles signed in; a game
   started meanwhile pauses it until closed). */
static void load_watch(void) {
  static uint32_t seen[4]; static int on = 0;
  char st[16] = "", rk[16] = ""; int busy = 0; cl_rd("state", st, sizeof st); cl_rd("run", rk, sizeof rk);
  if ((!strcmp(st, "running") || !strcmp(st, "paused")) && !strncmp(rk, "prep", 4)) busy = 1; if (file_exists("/data/round2/memq")) busy = 1;
  UserIdList l; memset(&l, 0xff, sizeof l); sceUserServiceGetLoginUserIdList(&l);
  if (!busy) { on = 0; return; }
  if (!on) { on = 1; for (int i = 0; i < 4; i++) seen[i] = (uint32_t)l.userId[i]; return; } /* who was already signed in when the load started */
  for (int i = 0; i < 4; i++) { uint32_t u = (uint32_t)l.userId[i]; if (u == 0xffffffffu || !u) continue; int old = 0; for (int j = 0; j < 4; j++) if (seen[j] == u) old = 1; if (old) continue;
    char nm[40] = ""; sceUserServiceGetUserName((int32_t)u, nm, sizeof nm); int of = 0, left = 0; char x[48] = ""; FILE *f = fopen("/data/round2/memqn", "r"); if (f) { if (fscanf(f, "%d %47s", &of, x) < 1) of = 0; fclose(f); }
    f = fopen("/data/round2/memq", "r"); if (f) { int c; while ((c = fgetc(f)) != EOF) if (c == '\n') left++; fclose(f); }
    char msg[200]; if (of > 1) snprintf(msg, sizeof msg, "Hang on %s - saves are still loading (player %d of %d). Please don't start a game yet.", nm, of - left, of);
    else snprintf(msg, sizeof msg, "Hang on %s - your saves are still loading. Please don't start a game yet.", nm);
    notify(msg); sv_log("load watch: %08x '%s' signed in during a member load - told to wait", u, nm);
    for (int j = 0; j < 4; j++) if (seen[j] == 0xffffffffu || !seen[j]) { seen[j] = u; break; } }
  for (int j = 0; j < 4; j++) { int still = 0; for (int i = 0; i < 4; i++) if ((uint32_t)l.userId[i] == seen[j]) still = 1; if (!still) seen[j] = 0xffffffffu; } /* signed out = may be told again */
}
static char sw_title[16]; static int sw_dirty; static long long sw_next; static char sw_name[64];
static void sw_game_name(const char *t, char *out, size_t n) {
  static uint8_t b[65536]; char p[160]; snprintf(p, sizeof p, "/user/app/%s/app.pkg", t); int len = sfo_from_pkg(p, b, sizeof b);
  out[0] = 0; if (len > 0) sfo_get(b, len, "TITLE", out, n); if (!out[0]) snprintf(out, n, "%s", t);
  for (char *c = out; *c; c++) if ((unsigned char)*c >= 128) *c = ' ';
}
static void sw_close(const char *key) { /* the watched game is no longer running: copy what changed */
  char t[16]; snprintf(t, sizeof t, "%s", sw_title); sw_title[0] = 0;
  if (clean_on || snap_on) { sv_log("save watch: %s closed during a clean slate/backup - that job copies it", t); return; }
  sleep(3); /* let the PS4 finish closing the save files */
  char who[120]; int ok = 0, bad = 0;
  bpop_key("sw", "%s closed - checking for new progress...", sw_name);
  int n = sw_scan(t, 1, key, &ok, &bad, who, sizeof who);
  sv_log("save watch: %s closed - %d changed part(s): %d copied, %d failed (%s)", t, n, ok, bad, who[0] ? who : "nobody");
  if (n) bpop_key("sw", "%s: %s's progress is saved on this PS4%s", sw_name, who, bad ? " (some parts failed - staff will check)" : file_exists("/data/round2/prep/cloud_on") ? " - sending it to the Round2 cloud next" : "");
  else bpop_key("sw", "%s closed - no new progress to save", sw_name);
}
static void sw_tick(const char *key) {
  char rt[32]; running_title(rt, sizeof rt); long long now = (long long)time(0);
  int game = rt[0] && strcmp(rt, VUE_ID) && strcmp(rt, "RTWO00001") && !r2_homebrew(rt) && !r2_protected(rt);
  if (sw_title[0] && (!game || strcmp(rt, sw_title))) sw_close(key); /* closed, or another game took its place (the tile doesn't count) */
  if (!game) return;
  if (!sw_title[0]) { snprintf(sw_title, sizeof sw_title, "%s", rt); sw_dirty = 0; sw_next = now + 900; sw_game_name(rt, sw_name, sizeof sw_name);
    sv_log("save watch: %s (%s) started - checking its saves every 15 min", rt, sw_name); bpop_key("sw", "%s started - your progress is saved to Round2 when you close the game", sw_name); }
  else if (!sw_dirty && now >= sw_next) { sw_next = now + 900; char who[120]; int ok = 0, bad = 0;
    if (sw_scan(rt, 0, key, &ok, &bad, who, sizeof who)) { sw_dirty = 1; sv_log("save watch: %s saves changed (%s) - copy when it closes", rt, who); bpop_key("sw", "%s: new progress for %s - it will be saved when the game is closed", sw_name, who); } }
}

/* ---- 1.1.50 CLEAN SLATE (owner 17 Nov 7:33pm: test a full clean slate on Kai before the 9pm guest, main profile included).
   "/_clean.<user>-<user>-..." lists every game of those profiles into /data/round2/clean/todo; a background job (like the
   snapshot: ~45 s a minute, paused while a game is open, carries on after restart/update) then, game by game: whole-game
   backup to the cloud (<console>/<user>/<game>/_game/<stamp>, restorable with /_gmove) and ONLY if that worked removes every
   part (save-list row + files); a failed backup leaves the game untouched and counts as failed. Never: PS Vue, the protected
   game in the main profile (GT Sport on Max). Pictures/names/order are separate commands (/_avreset, rename, /_uorder).
   "/_clean.stop" stops it. Progress in the report ("clean"), every step in save.log. */
#define CLND "/data/round2/clean"
/* 1.1.52: homebrew tools keep their own settings as "saves" (Kai 17 Nov: the clean slate wiped Apollo Save Tool's settings).
   Not guest saves - skipped by the clean slate and the purge. */
static int r2_homebrew(const char *t) { return !strncmp(t, "APOL", 4) || !strncmp(t, "LAPY", 4); }
static int cl_rd(const char *f, char *b, size_t n) { char p[100]; snprintf(p, sizeof p, CLND "/%s", f); FILE *x = fopen(p, "r"); if (!x) { b[0] = 0; return -1; }
  size_t r = fread(b, 1, n - 1, x); fclose(x); b[r] = 0; while (r && (b[r - 1] == '\n' || b[r - 1] == ' ')) b[--r] = 0; return 0; }
static void cl_wr(const char *f, const char *fmt, ...) { char p[100]; snprintf(p, sizeof p, CLND "/%s", f); FILE *x = fopen(p, "w"); if (!x) return;
  va_list ap; va_start(ap, fmt); vfprintf(x, fmt, ap); va_end(ap); fflush(x); fsync(fileno(x)); fclose(x); }
/* 1.1.53 CLEAN-SLATE LOG (owner 17 Nov 10:15pm: "a log in the control centre under the PS4 tab - summary of clean slate status
   and its errors"). history.txt = one plain line per run (clean / purge / reset), errors.txt = one line per problem; the last
   6 of each go in every report (log.clean, log.cleanerr) and the control centre shows them. */
static void cl_trim(const char *p, long keep) { struct stat st; if (stat(p, &st) || st.st_size < keep * 2) return;
  FILE *f = fopen(p, "r"); if (!f) return; char *b = malloc(keep + 1); if (!b) { fclose(f); return; }
  fseek(f, -keep, SEEK_END); size_t n = fread(b, 1, keep, f); fclose(f); b[n] = 0; char *s1 = strchr(b, '\n'); s1 = s1 ? s1 + 1 : b;
  f = fopen(p, "w"); if (f) { fputs(s1, f); fclose(f); } free(b); }
static void cl_stamp(char *o, size_t on, long long t) { time_t x = (time_t)t + 8 * 3600; struct tm tm; gmtime_r(&x, &tm); snprintf(o, on, "%02d/%02d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min); }
static void cl_err(const char *fmt, ...) {
  char m[220], ts[24]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
  cl_stamp(ts, sizeof ts, (long long)time(0)); mkdir(CLND, 0777);
  FILE *f = fopen(CLND "/errors.txt", "a"); if (f) { fprintf(f, "%s %s\n", ts, m); fclose(f); }
  cl_trim(CLND "/errors.txt", 6000);
  char b[16]; cl_rd("errn", b, sizeof b); cl_wr("errn", "%d\n", atoi(b) + 1);
  sv_log("clean ERROR: %s", m);
}
static void cl_hist(const char *line) { mkdir(CLND, 0777); FILE *f = fopen(CLND "/history.txt", "a"); if (f) { fprintf(f, "%s\n", line); fclose(f); } cl_trim(CLND "/history.txt", 8000); }
static void cl_begin(const char *kind, const char *label) { cl_wr("run", "%s %lld %s\n", kind, (long long)time(0), label && label[0] ? label : "-"); cl_wr("errn", "0\n"); cl_wr("copied", "0 0\n"); cl_wr("profiles", "\n"); }
static int clean_status(char *st, int *done, int *total, int *fails, int *parts) {
  char b[100]; if (cl_rd("state", b, sizeof b)) return 0; snprintf(st, 16, "%s", b);
  cl_rd("stat", b, sizeof b); sscanf(b, "%d %d %d %d", done, total, fails, parts); return 1;
}
/* 1.1.74 (owner 19 Nov 4:22pm, Kaeden test bench): "/_copy.all" = the clean slate's INTERNAL COPY step for every game of every
   profile, and NOTHING else - no removal, no profile changes, no sign-outs. Runs as the clean slate job (report "clean", kind
   "copy", /_clean.stop), ~45 s a minute, paused while a game is open. Each part ends up in /data/round2/prep/<user>/<game>/<part>/
   with manifest.txt (path, size, SHA-256) and is queued for the cloud batch (pending.txt). */
/* 1.1.77 (owner 19 Nov 5:17pm, Kaeden test bench): "/_restoreall" = the "walk-in member" download test. Every game in this PS4's
   restore point is moved back into the profile it came from, straight from the cloud (/_gmove: back up what the profile has,
   make missing save parts, download each file, write it in), one game at a time as a background job (report "clean", kind
   "restore", /_clean.stop). Each game is timed: download / into profile / making parts / backing up first - in save.log
   ("restore test: ...") and the pop-ups. Homebrew settings and PS Vue are skipped. */
static void look_restoreall(void) {
  char st[16] = "", lab[24] = "", stamp[24] = "", sst[16] = ""; int dn = 0, tt = 0, fl = 0; long long by = 0;
  cl_rd("state", st, sizeof st);
  if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("restoreall/refused", "a clean slate / copy is already running"); return; }
  if (!snap_status(lab, stamp, sst, &dn, &tt, &fl, &by) || strcmp(sst, "done")) { look_kv("restoreall/refused", "no finished restore point on this PS4"); return; }
  FILE *ix = fopen(SNAPD "/index", "r"); if (!ix) { look_kv("restoreall/error", "no restore point list"); return; }
  mkdir(CLND, 0777);
  FILE *td = fopen(CLND "/todo", "w"); if (!td) { fclose(ix); look_kv("restoreall/error", "can't write the list"); return; }
  char ln[200], u[16], t[16], nm[48], pu[16] = "", pt[16] = ""; int f, fl2, ng = 0; unsigned long long b;
  while (fgets(ln, sizeof ln, ix)) {
    if (sscanf(ln, "%15s %15s %47s %d %llu %d", u, t, nm, &f, &b, &fl2) != 6 || fl2 || r2_homebrew(t) || !strcmp(t, VUE_ID)) continue;
    if (!strcmp(u, pu) && !strcmp(t, pt)) continue;
    fprintf(td, "%s %s\n", u, t); ng++; snprintf(pu, sizeof pu, "%s", u); snprintf(pt, sizeof pt, "%s", t);
  }
  fclose(ix); fclose(td);
  cl_begin("restore", lab);
  cl_wr("pos", "0\n"); cl_wr("stat", "0 %d 0 0\n", ng); cl_wr("index", ""); cl_wr("rtime", "0\n"); cl_wr("mode", "restore %s\n", stamp);
  cl_wr("state", ng ? "running\n" : "done\n"); clean_on = ng > 0;
  sv_log("restore test started: %d games from restore point %s (%s)", ng, lab, stamp);
  char v[80]; snprintf(v, sizeof v, "%d games from restore point %s", ng, lab); look_kv("restoreall/started", v);
}
/* 1.1.82 (owner 19 Nov 7:21pm: "if we expect a member, the data should be in their profile beforehand"): STAGE 1 test of
   member prepare-ahead. "/_prepmember.<target profile>.<source profile>.<Name>" - every game of <source profile> in this PS4's
   restore point is moved into <target profile> (a spare, never the main), which is renamed <Name> (letters, digits, '_' =
   space; skipped if it is signed in). Same background job and timings as /_restoreall; the pop-up says when the member is ready.
   (Stage 2 will take the saves from the member's own cloud folder instead of a profile's restore point.) */
static void look_prepmember(const char *spec) {
  char tu[16] = "", su[16] = "", nm[40] = "", st[16] = "", lab[24] = "", stamp[24] = "", sst[16] = "", v[160]; int dn = 0, tt = 0, fl = 0; long long by = 0;
  if (sscanf(spec, "/_prepmember.%8[0-9a-f].%8[0-9a-f].%31[A-Za-z0-9_]", tu, su, nm) != 3) { look_kv("prep/error", "bad spec"); return; }
  uint32_t tid = (uint32_t)strtoul(tu, 0, 16);
  int tmain = tid == r2_main_user(); /* 1.1.83 (owner 19 Nov 7:38pm): members' saves go into the main profile too - it keeps the room's name */
  if (!prof_slot_base(tid)) { look_kv("prep/refused", "no such target profile"); return; }
  cl_rd("state", st, sizeof st);
  if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("prep/refused", "a clean slate / copy is already running"); return; }
  if (!snap_status(lab, stamp, sst, &dn, &tt, &fl, &by) || strcmp(sst, "done")) { look_kv("prep/refused", "no finished restore point on this PS4"); return; }
  FILE *ix = fopen(SNAPD "/index", "r"); if (!ix) { look_kv("prep/error", "no restore point list"); return; }
  mkdir(CLND, 0777);
  FILE *td = fopen(CLND "/todo", "w"); if (!td) { fclose(ix); look_kv("prep/error", "can't write the list"); return; }
  char ln[200], u[16], t[16], pn[48], pt[16] = ""; int f, fl2, ng = 0; unsigned long long b;
  while (fgets(ln, sizeof ln, ix)) {
    if (sscanf(ln, "%15s %15s %47s %d %llu %d", u, t, pn, &f, &b, &fl2) != 6 || fl2 || strcmp(u, su) || r2_homebrew(t) || !strcmp(t, VUE_ID) || !strcmp(t, pt)) continue;
    fprintf(td, "%s %s\n", tu, t); ng++; snprintf(pt, sizeof pt, "%s", t);
  }
  fclose(ix); fclose(td);
  char shown[40]; snprintf(shown, sizeof shown, "%s", nm); for (char *c = shown; *c; c++) if (*c == '_') *c = ' ';
  if (tmain) sv_log("member prep: %s is the main profile - keeps the room's name", tu);
  else if (!may_rename(tid)) sv_log("member prep: profile %s is signed in - name left as it is", tu);
  else { int r = sceUserServiceSetUserName((int32_t)tid, shown); sv_log("member prep: profile %s renamed to '%s' (0x%08x)", tu, shown, (unsigned)r); }
  cl_begin("prep", nm);
  cl_wr("pos", "0\n"); cl_wr("stat", "0 %d 0 0\n", ng); cl_wr("index", ""); cl_wr("rtime", "0\n"); cl_wr("mode", "restore %s %s\n", stamp, su);
  cl_wr("state", ng ? "running\n" : "done\n"); clean_on = ng > 0;
  sv_log("member prep started: '%s' into profile %s from %s's saves in restore point %s: %d games", shown, tu, su, lab, ng);
  bpop("Preparing member %s: %d games into their profile...", shown, ng);
  snprintf(v, sizeof v, "%d games for %s into profile %s", ng, shown, tu); look_kv("prep/started", v);
}
/* ---- 1.1.84 STAGE 2: member saves live in the member's own cloud folder (members/<id>/<slot>/...), not a PS4 profile's.
   "/_memtag.<profile>.<id>.<slot>" = this profile now belongs to member <id> (slot main / p1..p4): from the next clean slate its
   parts go to that member's folder ("/_memtag.<profile>.off" clears). "/_memload.<profile>.<id>.<slot>.<Name>" = every game in
   the member's index moved into <profile> (renamed <Name> unless it is the main profile, which keeps the room's name), then the
   profile is tagged to the member. Tags are cleared after a clean slate / reset / purge (the queued copies keep their tag). */
static int pf_off(void) { struct stat st; return !stat("/data/round2/prefetch-off", &st); }
/* fetch keys in bundles of up to 40 into the cache */
static void pf_fetch(const char *key, char (*ks)[400], int n) {
  for (int i = 0; i < n; i += 40) {
    int m = n - i > 40 ? 40 : n - i; const char *kp[40]; unsigned char *d[40]; size_t l[40];
    for (int j = 0; j < m; j++) kp[j] = ks[i + j];
    if (sv_getm(key, kp, m, d, l, 0, 0)) return;
    for (int j = 0; j < m; j++) if (d[j]) { if (pf_find(kp[j]) < 0) pf_add(kp[j], d[j], l[j]); else free(d[j]); }
  }
}
static void pf_game(const char *key, char (*parts)[220], int np) {
  pf_clear(); pf_hits = 0; if (pf_off() || np <= 0) return;
  static char ks[PF_MAX][400]; int n = 0;
  for (int i = 0; i < np && n < PF_MAX; i++) snprintf(ks[n++], 400, "%s/manifest.txt", parts[i]);
  pf_fetch(key, ks, n);
  /* labels + one-piece data files of every part, as long as it fits */
  int n2 = 0; unsigned long long tot = 0;
  for (int i = 0; i < np; i++) { int mi = pf_find(ks[i]); if (mi < 0) continue;
    char *m = malloc(pf[mi].l + 1); if (!m) continue; memcpy(m, pf[mi].d, pf[mi].l); m[pf[mi].l] = 0;
    for (char *ln = m, *nx; ln && *ln && n2 < PF_MAX - np; ln = nx) { nx = strchr(ln, '\n'); if (nx) *nx++ = 0;
      char *t1 = strchr(ln, '\t'); if (!t1) continue; *t1++ = 0; char *t2 = strchr(t1, '\t'); if (!t2) continue; *t2++ = 0;
      unsigned long long sz = strtoull(t1, 0, 10); int pcs = atoi_safe(t2); if (pcs != 1 || strstr(ln, "..") || tot + sz > (6u << 20)) continue;
      snprintf(ks[np + n2], 400, "%s/f/%s~0", parts[i], ln); n2++; tot += sz; }
    free(m); }
  pf_fetch(key, ks + np, n2);
  sv_log("prefetch: %d parts, %d files (%llu KB) in %d bundle request(s)", np, n2, tot / 1024, (np + 39) / 40 + (n2 + 39) / 40);
}
static int mem_ok(const char *x) { if (!x[0] || strlen(x) > 24) return 0; for (const char *c = x; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return 0; return 1; }
static void look_memtag(const char *spec) {
  char u[16] = "", id[32] = "", sl[16] = "", op[120], v[120];
  if (sscanf(spec, "/_memtag.%8[0-9a-f].%31[a-z0-9-].%15[a-z0-9]", u, id, sl) == 3 && mem_ok(id) && mem_ok(sl)) {
    snprintf(op, sizeof op, "/data/round2/prep/owner_%s.txt", u); mkdir("/data/round2/prep", 0777);
    snprintf(v, sizeof v, "%s/%s", id, sl); put_file(op, v); sv_log("member tag: profile %s -> member %s", u, v); look_kv("memtag/result", v); bpop("Profile now belongs to member %s", v); return; }
  if (sscanf(spec, "/_memtag.%8[0-9a-f].off", u) == 1) { snprintf(op, sizeof op, "/data/round2/prep/owner_%s.txt", u); unlink(op); sv_log("member tag: profile %s cleared", u); look_kv("memtag/result", "cleared"); return; }
  look_kv("memtag/error", "bad spec");
}
static int memload_game(const char *key, const char *u, const char *t, char *why, size_t wn) {
  pf_clear();
  static char ix[64 * 1024]; static char pn[64][64], pb[64][220], recv[64][32]; int np = 0;
  FILE *f = fopen(CLND "/memindex", "r"); if (!f) { snprintf(why, wn, "no member index"); return -1; }
  size_t il = fread(ix, 1, sizeof ix - 1, f); ix[il] = 0; fclose(f);
  for (char *ln = ix, *nx; ln && *ln && np < 64; ln = nx) { nx = strchr(ln, '\n'); if (nx) *nx++ = 0; char a[16], b[64], c[220]; if (sscanf(ln, "%15s %63s %219s", a, b, c) == 3 && !strcmp(a, t) && !strstr(c, "..")) { snprintf(pn[np], 64, "%s", b); snprintf(pb[np], 220, "%s", c); np++; } }
  if (!np) { snprintf(why, wn, "member has no parts for this game"); return -1; }
  if (r2_protected(t)) { snprintf(why, wn, "house save - never a member's"); return -1; }
  if (!game_installed(t)) { snprintf(why, wn, "game not installed in this room"); return -1; }
  if (!sv_buf) { void *bb = mmap(0, SV_PIECE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (bb == MAP_FAILED) { snprintf(why, wn, "no memory"); return -1; } sv_buf = bb; }
  int nr = sv_disk_parts(u, t, recv, 64);
  if (nr) { char rb[200]; int rn = 0; double tb0 = tm_now();
    mkdir("/data/round2/defer", 0777); snprintf(sv_defer_dir, sizeof sv_defer_dir, "/data/round2/defer/%s-%s-%lld", u, t, (long long)time(0)); mkdir(sv_defer_dir, 0777); sv_defer = 1;   /* 1.1.123 */
    int gbr = sv_game_back(key, u, t, rb, sizeof rb, &rn); sv_defer = 0; g_t_bk += tm_now() - tb0;
    if (gbr) { snprintf(why, wn, "backup of what was there failed - nothing written"); return -1; }
    put_file2(sv_defer_dir, "ready.txt", "1\n"); sv_log("member load: backup of what was in %s %s kept on the PS4 (%s) - to the cloud after the load", u, t, sv_defer_dir); }
  { double tp0 = tm_now(); pf_game(key, pb, np); g_t_dl += tm_now() - tp0; } /* 1.1.85 */
  double tmk0 = tm_now(); int fails = 0;
  int made[64] = { 0 };
  for (int i = 0; i < np; i++) { int have = 0; for (int j = 0; j < nr; j++) if (!strcmp(pn[i], recv[j])) have = 1;
    if (!have) { char w2[200] = ""; if (sv_make_part(key, u, t, pn[i], pb[i], w2, sizeof w2)) {
      if (!strcmp(w2, "part already exists")) { sv_log("member load: %s %s %s is already on the disk - using it (1.1.122: GTA V sce_bu_ backup part, Kaeden 20 Nov 8:36pm)", u, t, pn[i]); /* NOT marked made: a failed load must never delete it */ }
      else { fails++; sv_log("member load: making %s %s %s failed: %s", u, t, pn[i], w2); } } else made[i] = 1; } }
  g_t_mk += tm_now() - tmk0;
  if (fails) { snprintf(why, wn, "%d part(s) could not be made", fails); return -1; }
  int okp[64] = { 0 };
  for (int pass = 0; pass < 2; pass++) { fails = 0; /* 1.1.93: a part that fails gets one more try */
    for (int i = 0; i < np; i++) { if (okp[i]) continue; char rs[420]; snprintf(rs, sizeof rs, "/_srest.%s.%s.%s.%s", u, t, pn[i], pb[i]); if (look_srest(key, rs) != 0) fails++; else okp[i] = 1; }
    if (!fails) break; if (!pass) sv_log("member load: %s %s - %d part(s) failed, trying once more", u, t, fails); }
  if (fails) { /* 1.1.93: never leave a game half loaded (the game calls that "corrupted"): take back the parts made in this run */
    int back = 0; for (int i = 0; i < np; i++) if (made[i]) { char res[300]; if (!sv_del_part(u, t, pn[i], res, sizeof res)) back++; }
    sv_log("member load: %s %s NOT loaded - %d of %d parts failed; %d new part(s) taken back so the game starts clean (the member's cloud save is untouched)", u, t, fails, np, back);
    snprintf(why, wn, "%d of %d parts failed to load (game left clean)", fails, np); return -1; }
  for (int j = 0; j < nr; j++) { int keep = 0; for (int i = 0; i < np; i++) if (!strcmp(pn[i], recv[j])) keep = 1; if (!keep) { char res[300]; sv_del_part(u, t, recv[j], res, sizeof res); } }
  sv_log("member load: user %s game %s: %d parts in (%d answered from the prefetch)", u, t, np, pf_hits); pf_clear();
  return 0;
}
static void look_memload(const char *key, const char *spec) {
  char u[16] = "", id[32] = "", sl[16] = "", nm[40] = "", st[16] = "", k[200], v[160];
  if (sscanf(spec, "/_memload.%8[0-9a-f].%31[a-z0-9-].%15[a-z0-9].%31[A-Za-z0-9_]", u, id, sl, nm) != 4 || !mem_ok(id) || !mem_ok(sl)) { look_kv("memload/error", "bad spec"); return; }
  uint32_t uid = (uint32_t)strtoul(u, 0, 16); if (!prof_slot_base(uid)) { look_kv("memload/refused", "no such profile"); return; }
  cl_rd("state", st, sizeof st); if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("memload/refused", "a clean slate / copy is already running"); return; }
  snprintf(k, sizeof k, "members/%s/%s/index.txt", id, sl); unsigned char *ix = 0; size_t il = 0; char gw[130] = "";
  if (sv_get_raw(key, k, &ix, &il, gw, sizeof gw) || !il) { free(ix); look_kv("memload/refused", "member has no saves in the cloud yet"); return; }
  mkdir(CLND, 0777); { FILE *f = fopen(CLND "/memindex", "w"); if (!f) { free(ix); look_kv("memload/error", "can't write"); return; } fwrite(ix, 1, il, f); fclose(f); }
  FILE *td = fopen(CLND "/todo", "w"); if (!td) { free(ix); look_kv("memload/error", "can't write the list"); return; }
  int ng = 0; char seen[64][16]; int ns = 0;
  char *b = malloc(il + 1); if (b) { memcpy(b, ix, il); b[il] = 0;
    for (char *ln = b, *nx; ln && *ln; ln = nx) { nx = strchr(ln, '\n'); if (nx) *nx++ = 0; char a[16]; if (sscanf(ln, "%15s", a) != 1 || r2_protected(a) || r2_homebrew(a)) continue;
      int dup = 0; for (int i = 0; i < ns; i++) if (!strcmp(seen[i], a)) dup = 1; if (dup || ns >= 64) continue; snprintf(seen[ns++], 16, "%s", a); fprintf(td, "%s %s\n", u, a); ng++; }
    free(b); }
  fclose(td); free(ix);
  char shown[40]; snprintf(shown, sizeof shown, "%s", nm); for (char *c = shown; *c; c++) if (*c == '_') *c = ' ';
  if (may_rename(uid)) { bpop_key("ren", "Naming the profile '%s'", shown); int rr = sceUserServiceSetUserName((int32_t)uid, shown); if (user_signed_in(uid)) sv_log("member load: signed-in profile %08x renamed to '%s' (0x%08x)", uid, shown, (unsigned)rr); } /* 1.1.100 */
  { char op[120], tv[80]; snprintf(op, sizeof op, "/data/round2/prep/owner_%s.txt", u); snprintf(tv, sizeof tv, "%s/%s", id, sl); mkdir("/data/round2/prep", 0777); put_file(op, tv); }
  char lab[48]; snprintf(lab, sizeof lab, "%s", nm);
  cl_begin("prep", lab);
  cl_wr("pos", "0\n"); cl_wr("stat", "0 %d 0 0\n", ng); cl_wr("index", ""); cl_wr("rtime", "0\n"); cl_wr("mode", "memload %s %s\n", id, sl);
  cl_wr("state", ng ? "running\n" : "done\n"); clean_on = ng > 0;
  sv_log("member load started: %s/%s into profile %s ('%s'): %d games", id, sl, u, shown, ng);
  bpop("Loading member %s: %d games...", shown, ng);
  snprintf(v, sizeof v, "%d games of %s/%s into %s", ng, id, sl, u); look_kv("memload/started", v);
}
/* 1.1.90 (owner 20 Nov 12:05am: "logged in on the phone, no feedback from the tile"): "/_memplay.<member>.<slot>.<Name>" – a
   member pressed "Load" on their phone (book.round2.sg/m, opened from the tile's QR). Pick the profile: the one already linked
   to this player, else the first spare (never the main profile) that isn't signed in and isn't linked to someone else. Then
   /_memload (renames it <Name>, links it, loads every game the player has in the cloud); a player with nothing in the cloud
   yet gets the link + the name only (/_memtag). While a clean slate runs it answers "waits" – the engine tries again. Result
   keys memplay/loading | memplay/ready | memplay/waits | memplay/refused, value "<profile> <text>". */
static int av_apply(const char *key, uint32_t uid, const char *id, const char *sl);
/* 1.1.101 (owner 20 Nov 3:03am: "i logged in and the profile wasn't in order"): the profile screen lists the most recently signed-in
   profile first (setting 0x0900, see /_uorder). The room's order is main, then Player 1-4 (spares by id) – put back after every
   member load and checked once a minute (only re-dealt when it's out of order; logged). */
static void keep_order(int force) {
  (void)force;
  RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
  uint32_t mainu = r2_main_user(), sp[16]; int ns = 0; if (!mainu) return;
  for (int i = 0; i < 16; i++) { if (L.userId[i] == -1 || !L.userId[i]) continue; if ((uint32_t)L.userId[i] != mainu) sp[ns++] = (uint32_t)L.userId[i]; }
  for (int i = 0; i < ns; i++) for (int j = i + 1; j < ns; j++) if (sp[j] < sp[i]) { uint32_t x = sp[i]; sp[i] = sp[j]; sp[j] = x; }
  if (ns > 4) ns = 4; if (!ns) return;
  int ok = 1, prev, v = 0; long b = prof_slot_base(mainu); if (!b || sceRegMgrGetInt(b + 0x900, &v)) return; prev = v;
  for (int i = 0; i < ns && ok; i++) { long bb = prof_slot_base(sp[i]); int vv = 0; if (!bb || sceRegMgrGetInt(bb + 0x900, &vv)) return; if (vv >= prev) ok = 0; prev = vv; }
  if (ok) return;
  char spec[200]; int n = snprintf(spec, sizeof spec, "/_uorder.%08x", mainu); for (int i = 0; i < ns; i++) n += snprintf(spec + n, sizeof spec - n, "-%08x", sp[i]);
  bpop_key("ord", "Putting the profiles back in order: main profile first, then Player 1-%d", ns);
  int m0 = look_mute; look_mute = 1; look_last[0] = 0; look_uorder(spec); look_mute = m0; sv_log("profile order put back (main, Player 1-%d): %s", ns, look_last);
}
static void memplay_one(const char *key, const char *spec, int allow_main) {
  char id[32] = "", sl[16] = "", nm[40] = "", st[16] = "", want[64], v[200], ms[200];
  char pos[4] = "";
  if (sscanf(spec, "/_memplay.%31[a-z0-9-].%15[a-z0-9].%31[A-Za-z0-9_].%1[m1-4]", id, sl, nm, pos) < 3 || !mem_ok(id) || !mem_ok(sl)) { look_kv("memplay/error", "bad spec"); return; }
  cl_rd("state", st, sizeof st);
  if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("memplay/waits", "- the room is being reset first"); return; }
  snprintf(want, sizeof want, "%s/%s", id, sl);
  RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
  uint32_t mainu = r2_main_user(), sp[16], pick = 0; int ns = 0;
  for (int i = 0; i < 16; i++) { if (L.userId[i] == -1 || !L.userId[i]) continue; if ((uint32_t)L.userId[i] != mainu) sp[ns++] = (uint32_t)L.userId[i]; }
  for (int i = 0; i < ns; i++) for (int j = i + 1; j < ns; j++) if (sp[j] < sp[i]) { uint32_t x = sp[i]; sp[i] = sp[j]; sp[j] = x; }
  char tag[16][64]; for (int i = 0; i < ns; i++) { char op[120]; tag[i][0] = 0; snprintf(op, sizeof op, "/data/round2/prep/owner_%08x.txt", sp[i]); FILE *f = fopen(op, "r"); if (f) { if (fgets(tag[i], 64, f)) tag[i][strcspn(tag[i], "\r\n")] = 0; fclose(f); } }
  /* 1.1.97 (owner 20 Nov 1:19am): the profile comes from the batch – "m" = main, "1".."4" = the k-th ticked player goes to Player k
     (only Aliff ticked -> Player 1). The whole room is linked to the crew in look_memplay. */
  (void)allow_main; (void)tag;
  if (pos[0] == 'm') pick = mainu; else if (pos[0] >= '1' && pos[0] <= '4' && pos[0] - '0' <= ns) pick = sp[pos[0] - '1'];
  else if (!strcmp(sl, "main")) pick = mainu;
  if (!pick) { look_kv("memplay/refused", "- this room has no profile for that slot: ask staff"); return; }
  char u[16]; snprintf(u, sizeof u, "%08x", pick);
  char shown[40]; snprintf(shown, sizeof shown, "%s", nm); for (char *c = shown; *c; c++) if (*c == '_') *c = ' ';
  av_apply(key, pick, id, sl); /* 1.1.98: their picture first (a moment), then the saves */
  snprintf(ms, sizeof ms, "/_memload.%s.%s.%s.%s", u, id, sl, nm);
  look_mute = 1; look_last[0] = 0; look_memload(key, ms); look_mute = 0;
  if (!strncmp(look_last, "memload/started", 15)) { snprintf(v, sizeof v, "%s%s %s", pick == mainu ? "main " : "", u, look_last + 17); sv_log("member play: %s/%s '%s' -> profile %s, loading", id, sl, shown, u); look_kv("memplay/loading", v); return; }
  /* nothing in the cloud yet (a new member) – link + name only */
  snprintf(ms, sizeof ms, "/_memtag.%s.%s.%s", u, id, sl); look_mute = 1; look_memtag(ms); look_mute = 0;
  int r = (pick == mainu || !may_rename(pick)) ? -1 : sceUserServiceSetUserName((int32_t)pick, shown); /* the main keeps the room name */
  sv_log("member play: %s/%s '%s' -> profile %s, linked (no saves in the cloud yet; rename 0x%08x)", id, sl, shown, u, (unsigned)r);
  if (pick == mainu) bpop("%s: ready on the main profile - just play", shown); else bpop("%s: profile ready - sign in as %s", shown, shown);
  snprintf(v, sizeof v, "%s%s new member - profile linked%s", pick == mainu ? "main " : "", u, pick == mainu ? "" : " and named"); look_kv("memplay/ready", v);
}
/* 1.1.98 (owner 20 Nov 1:33am: "wire up the lapy avatars first as proof of concept"). Lapy's Avatar Changer keeps its pictures
   in /data/AVATARS as .xavatar files (a zip of avatar.png + avatar64/128/260/440.dds + picture.* + online.json). "/_avlib" uploads them
   to the cloud as lib/av/NNNN.xavatar (+ lib/av/index.txt "NNNN<tab>file name", names in sorted order) so the member page can
   show them. When a member picks one the engine unzips it into members/<id>/<slot>/avatar.bin ("R2AV <n>\n" then per file
   "<name> <length>\n<bytes>"); av_apply puts those files on the profile at Load (signed-out spare only, never the main). */
/* 1.1.100 (owner 20 Nov 2:43am: "the profile doesn't update to the correct names"): Player 1/2 were SIGNED IN, and we never
   renamed a signed-in profile (only our own safety rule, never tested). "/_rename.<user>.<Name>" renames even a signed-in spare
   and reads the name back. Once /data/round2/rename-signedin exists ("/_renamesi.on"), member loads rename signed-in spares too. */
static int ren_si(void) { struct stat st; return !stat("/data/round2/rename-signedin", &st); }
static int may_rename(uint32_t uid) { return uid != r2_main_user() && (!user_signed_in(uid) || ren_si()); }
/* 1.1.103 (owner 20 Nov 3:50am: "probe the sceShellui and make logs using the helper"): GoldHEN's own SDK call (syscall 500,
   cmd 6 = load a module into a running process) puts round2_shprobe.prx into SceShellUI. The probe only LISTS the names of the
   menu's user / controller / PS-button / log-in-out functions into /data/round2/shprobe.txt – nothing is called or changed.
   Refused while a game is open (if the menu restarts, a guest's game would close). "/_ghver" = is GoldHEN's SDK call there. */
struct r2_prx_load { char process_name[32]; char prx_path[100]; uint64_t res; } __attribute__((packed));
/* 1.1.104: GoldHEN's own way in (indirect syscall 0 -> 500, carry flag = error), as in GoldHEN_Plugins_SDK Syscall.c – libc's
   syscall() answered -256 on Kaeden */
__asm__(".intel_syntax noprefix\n.global r2_osyscall\nr2_osyscall:\n  mov rax, 0\n  mov r10, rcx\n  syscall\n  jb 1f\n  ret\n1:\n  neg rax\n  ret\n.att_syntax prefix\n");
long r2_osyscall(long num, ...);
static long r2_sdk(uint64_t cmd, void *data) { return r2_osyscall(500, cmd, data); }
static void look_ghver(void) { char v[160]; long r = r2_sdk(0, 0); snprintf(v, sizeof v, "%ld (0x%lx)", r, (unsigned long)r); look_kv("gh/sdk-version", v);
  struct { int pid; char name[40]; char path[64]; char titleid[16]; char contentid[64]; char version[6]; uint64_t base_address; } __attribute__((packed)) pi;
  memset(&pi, 0, sizeof pi); pi.pid = getpid(); long q = r2_sdk(4, &pi); snprintf(v, sizeof v, "%ld: pid %d name '%.39s' title '%.15s'", q, pi.pid, pi.name, pi.titleid); look_kv("gh/proc-info", v); }
static void look_shprobe(const char *key) {
  if (sv_game_open()) { look_kv("shprobe/refused", "a game is open - close it first"); return; }
  long ver = r2_sdk(0, 0); char v[200]; snprintf(v, sizeof v, "%ld", ver); look_kv("shprobe/sdk", v);
  /* 1.1.106: 1.1.105 got 0x80020002 (file not found) for /data/... – the menu sees the disk differently. Try the real paths too. */
  /* 1.1.107: 1.1.106 got /data = not found, /user/data = no permission, /system_tmp = 0x80020023 -> files readable by everyone
     (chmod 0777) and also in GoldHEN's own plugin folder, which GoldHEN itself loads from */
  static const char *outs[] = { "/data/round2/shprobe.txt", "/user/data/round2/shprobe.txt", "/user/data/GoldHEN/shprobe.txt", "/data/GoldHEN/shprobe.txt", "/system_tmp/shprobe.txt", 0 };
  for (int i = 0; outs[i]; i++) { char d[80]; unlink(outs[i]); snprintf(d, sizeof d, "%s.done", outs[i]); unlink(d); }
  static const char *prx[] = { "/user/data/GoldHEN/plugins/r2shprobe.prx", "/data/GoldHEN/plugins/r2shprobe.prx", "/user/data/round2/r2shprobe.prx", "/system_tmp/r2shprobe.prx", 0 };
  { FILE *f = fopen("/data/round2/r2shprobe.prx", "wb"); if (!f || fwrite(r2_shprobe_prx, 1, r2_shprobe_prx_len, f) != r2_shprobe_prx_len) { if (f) fclose(f); look_kv("shprobe/error", "could not write the probe"); return; } fclose(f); }
  { FILE *f = fopen("/system_tmp/r2shprobe.prx", "wb"); if (f) { fwrite(r2_shprobe_prx, 1, r2_shprobe_prx_len, f); fclose(f); } }
  { mkdir("/data/GoldHEN/plugins", 0777); FILE *f = fopen("/data/GoldHEN/plugins/r2shprobe.prx", "wb"); if (f) { fwrite(r2_shprobe_prx, 1, r2_shprobe_prx_len, f); fclose(f); } }
  chmod("/data/round2", 0777); chmod("/data/round2/r2shprobe.prx", 0777); chmod("/data/GoldHEN/plugins/r2shprobe.prx", 0777); chmod("/system_tmp/r2shprobe.prx", 0777);
  char res[200] = ""; int loaded = 0;
  for (int i = 0; prx[i] && !loaded; i++) {
    struct r2_prx_load a; memset(&a, 0, sizeof a); snprintf(a.process_name, sizeof a.process_name, "SceShellUI"); snprintf(a.prx_path, sizeof a.prx_path, "%s", prx[i]);
    long r = r2_sdk(6, &a); size_t l = strlen(res); snprintf(res + l, sizeof res - l, "%s%d:%lx", i ? " " : "", i, (unsigned long)(uint32_t)a.res);
    sv_log("shprobe: load %s into SceShellUI: call %ld, result 0x%llx", prx[i], r, (unsigned long long)a.res);
    if (r == 0 && (int64_t)a.res > 0 && (int64_t)a.res < 0x80000000LL) loaded = 1;
  }
  look_kv("shprobe/load", res);
  const char *got = 0; for (int s2 = 0; s2 < 30 && !got; s2++) { for (int i = 0; outs[i] && !got; i++) { char d[80]; snprintf(d, sizeof d, "%s.done", outs[i]); if (file_exists(d)) got = outs[i]; } if (!got) sleep(1); }
  const char *out = got; if (!out) for (int i = 0; outs[i] && !out; i++) if (file_exists(outs[i])) out = outs[i];
  struct stat st; long sz = (!out || stat(out, &st)) ? -1 : (long)st.st_size;
  snprintf(v, sizeof v, "%s, %s, %ld bytes", got ? "finished" : "not finished in 30 s", out ? out : "no file", sz); look_kv("shprobe/result", v); sv_log("shprobe: %s", v);
  if (sz > 0) { size_t n = 0; char *b = read_all(out, &n, 1024 * 1024); if (b) { char k[120]; snprintf(k, sizeof k, "lib/probe/shprobe-%s.txt", console_id); int u = sv_put(key, k, (unsigned char *)b, n); free(b); look_kv("shprobe/upload", u ? "failed" : k); } }
}
/* 1.1.109: "/_shmod.<name>" = fetch a Round2 system-menu add-on (round2_*.prx, branch ps4-shell, via the engine's /ps4/prx) into
   GoldHEN's plugin folder - the only place the menu can load from (1.1.107) - and load it into SceShellUI. The add-on writes
   /data/round2/<name>.txt (+ .done); that text goes to R2 lib/probe/<name>-<console>.txt. Refused while a game is open.
   Add-on changes then need no helper update (owner 20 Nov: keep helper switches per PS4 start low). */
static void look_shmod(const char *key, const char *spec) {
  char name[40] = "", v[220], path[120], up[120], hdr[200], err[160]; if (sscanf(spec, "/_shmod.%39[a-z0-9_]", name) != 1 || strncmp(name, "round2_", 7)) { look_kv("shmod/error", "bad name"); return; }
  if (sv_game_open()) { look_kv("shmod/refused", "a game is open - close it first"); return; }
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\n", key); snprintf(path, sizeof path, "/ps4/prx?name=%s", name);
  unsigned char *d = 0; size_t len = 0; int st = r2tls_fetch(ENGINE_HOST, "GET", path, hdr, 0, 0, &d, &len, 4 * 1024 * 1024, err, sizeof err);
  if (st != 200 || !d || len < 64 || (memcmp(d, "\x7f" "ELF", 4) && memcmp(d, "\x4f\x15\x3d\x1d", 4))) { /* 1.1.110: a .prx is a SELF (4F 15 3D 1D), not a bare ELF */ snprintf(v, sizeof v, "download %d %s (%zu bytes)", st, err, len); free(d); look_kv("shmod/error", v); return; }
  mkdir("/data/GoldHEN", 0777); mkdir("/data/GoldHEN/plugins", 0777); snprintf(up, sizeof up, "/data/GoldHEN/plugins/%s.prx", name);
  unlink(up); FILE *f = fopen(up, "wb"); int okw = f && fwrite(d, 1, len, f) == len; if (f) fclose(f); free(d); chmod(up, 0777);
  if (!okw) { look_kv("shmod/error", "could not save it"); return; }
  char out[120], don[130]; snprintf(out, sizeof out, "/data/round2/%s.txt", name); snprintf(don, sizeof don, "%s.done", out); unlink(out); unlink(don);
  struct r2_prx_load a; memset(&a, 0, sizeof a); snprintf(a.process_name, sizeof a.process_name, "SceShellUI"); snprintf(a.prx_path, sizeof a.prx_path, "/user/data/GoldHEN/plugins/%s.prx", name);
  long r = r2_sdk(6, &a); snprintf(v, sizeof v, "%zu bytes, call %ld, result 0x%llx", len, r, (unsigned long long)a.res); look_kv("shmod/load", v); sv_log("shmod: %s into SceShellUI: %s", name, v);
  int done = 0; for (int i = 0; i < 30 && !(done = file_exists(don)); i++) sleep(1);
  struct stat ss; long sz = stat(out, &ss) ? -1 : (long)ss.st_size; snprintf(v, sizeof v, "%s, %ld bytes", done ? "finished" : "not finished in 30 s", sz); look_kv("shmod/result", v); sv_log("shmod: %s %s", name, v);
  if (sz > 0) { size_t n = 0; char *b = read_all(out, &n, 4 * 1024 * 1024); /* 1.1.111: was 1 MB - round2_sweep1 is 1.8 MB */ if (b) { char k[120]; snprintf(k, sizeof k, "lib/probe/%s-%s.txt", name, console_id); int u2 = sv_put(key, k, (unsigned char *)b, n); free(b); look_kv("shmod/upload", u2 ? "failed" : k); } }
}
/* 1.1.114 (owner 20 Nov 3:16pm, after the Kaeden restart): the Round2 menu add-on (round2_shell: picker pause + PS button lock during
   a member load, everyone logged out after it) is loaded at every PS4 start - no "/_shmod" by hand. Rules:
   - skipped while /data/round2/shell-off exists, while a game is open (tried again next minute), and in the 5 min after a wake;
   - skipped while the add-on of this name says it's alive (/data/round2/shell.alive less than 3 min old + shell.gen = the name) -
     so a helper update doesn't load it twice (a module path loads only once per menu start anyway);
   - at most 3 tries per helper run, 10 min apart; every try and result goes to save.log (owner: log everything).
   The add-on's own self-test (60 s lock, then log-out) only runs on "/_shtest" - never at start-up. */
#define SHELL_ADDON "round2_shell9"
static int shell_tries = 0; static time_t shell_last_try = 0;
static int shell_alive(void) {
  char g[24] = ""; FILE *f = fopen("/data/round2/shell.gen", "r"); if (f) { if (!fgets(g, sizeof g, f)) g[0] = 0; fclose(f); }
  struct stat st; return !strncmp(g, SHELL_ADDON, strlen(SHELL_ADDON)) && !stat("/data/round2/shell.alive", &st) && time(0) - st.st_mtime < 180;
}
static int shell_load(const char *key, char *res, size_t rl) {   /* download + load into SceShellUI; 0 = loaded */
  char path[120], up[120], hdr[200], err[160]; snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\n", key); snprintf(path, sizeof path, "/ps4/prx?name=%s", SHELL_ADDON);
  unsigned char *d = 0; size_t len = 0; int st = r2tls_fetch(ENGINE_HOST, "GET", path, hdr, 0, 0, &d, &len, 4 * 1024 * 1024, err, sizeof err);
  if (st != 200 || !d || len < 64 || (memcmp(d, "\x7f" "ELF", 4) && memcmp(d, "\x4f\x15\x3d\x1d", 4))) { snprintf(res, rl, "download %d %s (%zu bytes)", st, err, len); free(d); return -1; }
  mkdir("/data/GoldHEN", 0777); mkdir("/data/GoldHEN/plugins", 0777); snprintf(up, sizeof up, "/data/GoldHEN/plugins/%s.prx", SHELL_ADDON);
  unlink(up); FILE *f = fopen(up, "wb"); int okw = f && fwrite(d, 1, len, f) == len; if (f) fclose(f); free(d); chmod(up, 0777);
  if (!okw) { snprintf(res, rl, "could not save it"); return -1; }
  struct r2_prx_load a; memset(&a, 0, sizeof a); snprintf(a.process_name, sizeof a.process_name, "SceShellUI"); snprintf(a.prx_path, sizeof a.prx_path, "/user/data/GoldHEN/plugins/%s.prx", SHELL_ADDON);
  long r = r2_sdk(6, &a); int ok = 0; for (int i = 0; i < 15 && !(ok = shell_alive()); i++) sleep(1);
  snprintf(res, rl, "%zu bytes, call %ld, result 0x%llx, %s", len, r, (unsigned long long)a.res, ok ? "running (alive)" : "not alive after 15 s");
  return ok ? 0 : -1;
}
static void shell_auto(const char *key) {
  if (!key[0] || file_exists("/data/round2/shell-off") || shell_alive()) return;
  { struct stat st; if (!stat("/data/round2/shell.alive", &st) && time(0) - st.st_mtime < 90) { static int said; if (!said++) sv_log("menu add-on %s: an older add-on is still running in the menu - the new one loads at the next PS4 start", SHELL_ADDON); return; } } /* 1.1.125: shell8 + shell9 ran side by side on Kaeden 20 Nov 9:11pm */
  if (shell_tries >= 3 || (shell_last_try && time(0) - shell_last_try < 600)) return;
  if (wake_at && time(0) - wake_at < 300) return;
  if (sv_game_open()) return;
  shell_tries++; shell_last_try = time(0); char res[220]; int r = shell_load(key, res, sizeof res);
  sv_log("menu add-on %s auto-load (try %d): %s%s", SHELL_ADDON, shell_tries, res, r ? " - NOT tried again until the helper restarts (a failed load can mean a menu crash)" : "");
  shell_tries = 3; /* 1.1.117 (Kaeden 7:10pm crash): one try per helper run, whatever happened */
}
/* 1.1.114: "/_shtest" = the add-on's 60-s self-test with the owner at the PS4 (picker paused + PS button locked, every button change
   logged, L2+R2+L3+R3 held 1 s unlocks, then everyone logged out), then its log goes to R2 lib/probe/<add-on>-<console>.txt.
   Loads the add-on first if it isn't running. Refused while a game is open. */
static void look_shtest(const char *key) {
  if (sv_game_open()) { look_kv("shtest/refused", "a game is open - close it first"); return; }
  put_file("/data/round2/shell-selftest", "1\n");
  if (!shell_alive()) { char res[220]; int r = shell_load(key, res, sizeof res); look_kv("shtest/load", res); sv_log("shtest: load %s: %s", SHELL_ADDON, res); if (r) return; }
  else look_kv("shtest/load", "already running");
  char lp[120]; snprintf(lp, sizeof lp, "/data/round2/%s.txt", SHELL_ADDON); int fin = 0;
  size_t n0 = 0; { char *b0 = read_all(lp, &n0, 4 * 1024 * 1024); free(b0); }   /* 1.1.118: only look at what this run adds (the 7:30pm test "finished" at once on the 7:27 test's lines) */
  for (int i = 0; i < 100 && !fin; i++) { sleep(1); size_t n = 0; char *b = read_all(lp, &n, 4 * 1024 * 1024); if (b) { char *q = n > n0 ? strstr(b + n0, "SELF-TEST") : 0; fin = q && strstr(q, "3 s after log-out"); free(b); } }
  look_kv("shtest/result", fin ? "self-test finished" : "not finished in 100 s");
  size_t n = 0; char *b = read_all(lp, &n, 4 * 1024 * 1024);
  if (b) { char k[120]; snprintf(k, sizeof k, "lib/probe/%s-%s.txt", SHELL_ADDON, console_id); int u = sv_put(key, k, (unsigned char *)b, n); free(b); look_kv("shtest/upload", u ? "failed" : k); }
  sv_log("shtest: %s", fin ? "self-test finished" : "not finished in 100 s");
}
/* 1.1.111 (owner 20 Nov 6:01am, the one-time sweep of every PS4): "/_probeup.<name>" = upload an add-on's result file again
   (/data/round2/<name>.txt + its breadcrumb <name>.step) WITHOUT loading the add-on again – e.g. after a cut upload or a menu
   crash (the .step file says which step it died in). Read-only. */
static void look_probeup(const char *key, const char *spec) {
  char name[40] = "", p[120], k[160], v[120]; if (sscanf(spec, "/_probeup.%39[a-z0-9_]", name) != 1 || strncmp(name, "round2_", 7)) { look_kv("probeup/error", "bad name"); return; }
  const char *ext[] = { "txt", "step", 0 };
  for (int i = 0; ext[i]; i++) {
    snprintf(p, sizeof p, "/data/round2/%s.%s", name, ext[i]); size_t n = 0; char *b = read_all(p, &n, 4 * 1024 * 1024);
    if (!b) { snprintf(k, sizeof k, "probeup/%s", ext[i]); look_kv(k, "no such file"); continue; }
    snprintf(k, sizeof k, "lib/probe/%s-%s.%s", name, console_id, ext[i]); int r = sv_put(key, k, (unsigned char *)b, n);
    if (i == 1) { char st[100]; snprintf(st, sizeof st, "%.90s", b); for (char *q = st; *q; q++) if (*q == '\n') *q = ' '; look_kv("probeup/last-step", st); }
    free(b); snprintf(v, sizeof v, "%zu bytes %s", n, r ? "upload FAILED" : k); snprintf(k, sizeof k, "probeup/%s", ext[i]); look_kv(k, v);
  }
}
/* 1.1.111: "/_pwrreg" = READ-ONLY: the PS4's rest-mode / wake / Wi-Fi settings, by the registry numbers round2_sweep1 found inside
   the system menu (Kaeden 13.50; each PS4's own sweep file confirms the numbers for its firmware – a wrong number just reads
   nothing). Names as in Sony's registry list. Changes nothing. */
static void look_pwrreg(void) {
  static const struct { const char *n; long id; int bin; } R[] = {
    { "SYSTEM_POWER_charge (USB power in rest)", 0x2820100, 0 }, { "SYSTEM_POWER_signin (stay connected to the internet)", 0x2820200, 0 },
    { "SYSTEM_POWER_remote (turn on PS4 from network)", 0x2820300, 0 }, { "SYSTEM_POWER_controller_off", 0x2820400, 0 },
    { "SYSTEM_POWER_auto_off_media", 0x2820500, 0 }, { "SYSTEM_POWER_auto_off_other", 0x2820600, 0 },
    { "SYSTEM_POWER_auto_power_down", 0x2820700, 0 }, { "SYSTEM_POWER_suspend_to_ram (rest mode)", 0x2820800, 0 },
    { "SYSTEM_POWER_standby_start", 0x2820900, 8 }, { "SYSTEM_POWER_power_history", 0x2820b00, 0 },
    { "SYSTEM_POWER_spwakeup", 0x2820c00, 0 }, { "SYSTEM_POWER_shutdown_status", 0x2820e00, 0 },
    { "SYSTEM_POWER_charge_minutes", 0x2828000, 0 }, { "SYSTEM_eapfunction", 0x2070000, 0 },
    { "SYSTEM_SMR_HDD_eap_boot_from_mos", 0x2be0100, 0 }, { "SYSTEM_UPDATE_auto_download", 0x2800500, 0 },
    { "SYSTEM_DOWNLOAD_autodl_featured", 0x2880100, 0 }, { "NP_patch_auto_dl", 0x19010000, 0 },
    { "NET_COMMON_device (0 cable, 1 wifi)", 0x14190100, 0 }, { "NET_COMMON_net_flag", 0x14190600, 0 },
    { "NET_COMMON_conf_type", 0x14190700, 0 }, { "NET_WIFI_freq_band (Wi-Fi band setting)", 0x141e0500, 0 },
    { "NET_WIFI_IP_ip_config", 0x141e6001, 0 }, { "NET_WIFI_IP_leased_exp_tick", 0x141e600c, 8 },
    { "NET_always_lan", 0x147a0000, 0 }, { "REMOTEPLAY_rp_enable", 0x41810000, 0 }, { "REMOTEPLAY_ap_flag", 0x41820000, 0 },
    { 0, 0, 0 } };
  char k[96], v[80];
  for (int i = 0; R[i].n; i++) {
    snprintf(k, sizeof k, "pwr/%08lx %s", R[i].id, R[i].n);
    if (R[i].bin) { unsigned char b[8] = { 0 }; int r = sceRegMgrGetBin(R[i].id, b, 8); if (r) snprintf(v, sizeof v, "read error 0x%x", (unsigned)r);
      else { unsigned long long x = 0; for (int j = 7; j >= 0; j--) x = (x << 8) | b[j]; snprintf(v, sizeof v, "bin %llu (0x%llx)", x, x); } }
    else { int x = 0; int r = sceRegMgrGetInt(R[i].id, &x); if (r) snprintf(v, sizeof v, "read error 0x%x", (unsigned)r); else snprintf(v, sizeof v, "%d (0x%x)", x, (unsigned)x); }
    look_kv(k, v);
  }
  snprintf(v, sizeof v, "%d", sceSystemStateMgrIsStandbyModeEnabled()); look_kv("pwr/IsStandbyModeEnabled", v);
}
static void look_rename(const char *spec) {
  char u[16] = "", nm[40] = "", v[160], have[40] = ""; if (sscanf(spec, "/_rename.%8[0-9a-f].%31[A-Za-z0-9_]", u, nm) != 2) { look_kv("rename/error", "bad spec"); return; }
  uint32_t uid = (uint32_t)strtoul(u, 0, 16); if (uid == r2_main_user()) { look_kv("rename/refused", "never the main profile"); return; }
  for (char *c = nm; *c; c++) if (*c == '_') *c = ' ';
  int si = user_signed_in(uid), r = sceUserServiceSetUserName((int32_t)uid, nm); sceUserServiceGetUserName((int32_t)uid, have, sizeof have);
  snprintf(v, sizeof v, "%s: rc 0x%08x, name now '%s'", si ? "signed in" : "signed out", (unsigned)r, have); sv_log("rename test %08x -> '%s': %s", uid, nm, v); look_kv("rename/result", v);
}
static int av_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void look_avlib(const char *key, const char *spec) {
  int from = 1; sscanf(spec, "/_avlib.%d", &from); if (from < 1) from = 1;
  DIR *d = opendir("/data/AVATARS"); if (!d) { look_kv("avlib/error", "no /data/AVATARS on this PS4"); return; }
  static char *nm[1200]; int n = 0; struct dirent *e;
  while ((e = readdir(d)) && n < 1200) { size_t l = strlen(e->d_name); if (l > 8 && l < 120 && !strcasecmp(e->d_name + l - 8, ".xavatar")) nm[n++] = strdup(e->d_name); }
  closedir(d); qsort(nm, n, sizeof nm[0], av_cmp);
  unsigned char *b = malloc(SV_PIECE); size_t ixn = 0, ixc = 64 * 1024; char *ix = malloc(ixc); ix[0] = 0;
  int up = 0, bad = 0, big = 0; time_t t0 = time(0);
  for (int i = 0; i < n && b && ix; i++) {
    int w = snprintf(ix + ixn, ixc - ixn, "%04d\t%s\n", i + 1, nm[i]); if (w > 0 && (size_t)w < ixc - ixn) ixn += (size_t)w;
    if (i + 1 < from) continue;
    char p[200], k[64]; snprintf(p, sizeof p, "/data/AVATARS/%s", nm[i]); snprintf(k, sizeof k, "lib/av/%04d.xavatar", i + 1);
    FILE *f = fopen(p, "rb"); if (!f) { bad++; continue; }
    size_t r = fread(b, 1, SV_PIECE, f); int more = fgetc(f) != EOF; fclose(f);
    if (more) { big++; continue; }
    if (sv_put(key, k, b, r)) { bad++; sv_log("avlib: %s upload failed", nm[i]); } else up++;
    if (up % 25 == 0) { sceSystemServicePowerTick(); sv_log("avlib: %d of %d up (%lds)", up, n - from + 1, (long)(time(0) - t0)); }
  }
  if (ix && sv_put(key, "lib/av/index.txt", (unsigned char *)ix, ixn)) bad++;
  for (int i = 0; i < n; i++) free(nm[i]); free(b); free(ix);
  char v[160]; snprintf(v, sizeof v, "%d avatars found, %d uploaded, %d failed, %d too big, %lds", n, up, bad, big, (long)(time(0) - t0));
  sv_log("avlib: %s", v); look_kv(bad ? "avlib/partial" : "avlib/done", v);
}
static int av_apply(const char *key, uint32_t uid, const char *id, const char *sl) {
  if (uid == r2_main_user()) return 0;
  char k[120], why[160]; unsigned char *data = 0; size_t len = 0; snprintf(k, sizeof k, "members/%s/%s/avatar.bin", id, sl);
  if (sv_get_raw(key, k, &data, &len, why, sizeof why)) { sv_log("avatar: %s/%s has no picture yet (%s)", id, sl, why); free(data); return 0; }
  if (user_signed_in(uid)) { sv_log("avatar: %08x is signed in - picture not changed", uid); free(data); return -1; }
  int nf = 0; if (len < 8 || memcmp(data, "R2AV ", 5) || (nf = atoi((char *)data + 5)) < 1 || nf > 20) { sv_log("avatar: %s bad bundle", k); free(data); return -1; }
  { char pn[40]; sceUserServiceGetUserName((int32_t)uid, pn, sizeof pn); bpop_key("av", "Putting the chosen picture on profile '%s'", pn); }
  char td[120], rv[200]; av_dir(uid, td, sizeof td); sv_rights_up(rv, sizeof rv); mkdir(td, 0777); av_keep(uid, td);
  { DIR *x = opendir(td); struct dirent *e; while (x && (e = readdir(x))) if (av_name(e->d_name)) { char p[300]; snprintf(p, sizeof p, "%s/%s", td, e->d_name); unlink(p); } if (x) closedir(x); }
  size_t o = (size_t)((unsigned char *)memchr(data, '\n', len) - data) + 1; int cp = 0, bad = 0;
  for (int i = 0; i < nf && o < len; i++) {
    char nm[48] = ""; unsigned long fl = 0; unsigned char *nl = memchr(data + o, '\n', len - o); if (!nl) { bad++; break; }
    *nl = 0; if (sscanf((char *)data + o, "%47s %lu", nm, &fl) != 2 || !av_name(nm) || strchr(nm, '/')) { bad++; break; }
    o = (size_t)(nl - data) + 1; if (o + fl > len) { bad++; break; }
    char p[300]; snprintf(p, sizeof p, "%s/%s", td, nm); FILE *f = fopen(p, "wb");
    if (f && fwrite(data + o, 1, fl, f) == fl) cp++; else bad++; if (f) fclose(f); o += fl;
  }
  sv_rights_back(); free(data);
  sv_log("avatar: %s/%s -> profile %08x: %d file(s) written, %d failed", id, sl, uid, cp, bad);
  return bad ? -1 : cp;
}
/* 1.1.94 (owner 20 Nov 12:53am: tick several crew players and load them at once): "/_memplay.<member>.<slot>.<Name>[.<slot>.<Name>…]"
   (up to 4). The first player starts now; the others wait in /data/round2/memq and start one after another when the load
   before them is done (clean_step) – a player with nothing in the cloud yet is linked and named at once. */
#define MEMQ "/data/round2/memq"
static void memplay_next(const char *key) {
  char b[600] = "", ln[200]; FILE *f = fopen(MEMQ, "r"); if (!f) return;
  size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f);
  char *nl = strchr(b, '\n'); if (!b[0]) { unlink(MEMQ); return; }
  if (nl) { *nl = 0; snprintf(ln, sizeof ln, "%s", b); put_file(MEMQ, nl + 1); if (!nl[1]) unlink(MEMQ); } else { snprintf(ln, sizeof ln, "%s", b); unlink(MEMQ); }
  char id[32], sl[16], nm[40], ps[4] = "", spec[160]; if (sscanf(ln, "%31s %15s %39s %3s", id, sl, nm, ps) < 3) return;
  snprintf(spec, sizeof spec, "/_memplay.%s.%s.%s%s%s", id, sl, nm, ps[0] ? "." : "", ps);
  { int of = 0; char x[48]; FILE *qf = fopen("/data/round2/memqn", "r"); if (qf) { if (fscanf(qf, "%d %47s", &of, x) < 1) of = 0; fclose(qf); } qf = fopen("/data/round2/memqn", "w"); if (qf) { fprintf(qf, "%d %s\n", of, nm); fclose(qf); } }
  int m0 = look_mute; look_mute = 1; look_last[0] = 0; memplay_one(key, spec, 0); look_mute = m0;
  sv_log("member play queue: %s %s -> %s", id, sl, look_last);
  if (!strncmp(look_last, "memplay/ready", 13) || !strncmp(look_last, "memplay/refused", 15) || !strncmp(look_last, "memplay/error", 13)) { if (file_exists(MEMQ)) memplay_next(key); else { unlink("/data/round2/memqn"); keep_order(1); } } /* nothing started - go on */
}
/* 1.1.124 (owner 20 Nov 9:03pm: loading only Aliff renamed Karen's profile to Aliff and left the old "Aliff Bozz" in Player 2 –
   "its considered a switch profile, sometimes guest may want to do it too, so load their profiles and clean slate the rest"):
   before a member load, every room profile NOT taking a ticked player is RELEASED: its games get the normal clean slate (internal
   copy queued for the cloud of the member it belonged to, then removed – never PS Vue / the protected main-profile game), then
   spares are renamed back to "Player k", pictures reset, member tags removed. The ticked players then load as before. While this
   runs the queue file holds a placeholder line, so the engine/tile keep showing "Getting <name>'s profile ready...". */
#define MEMAFTER "/data/round2/memplay-after"
static int prof_games(const char *u) {   /* how many games this profile has saves for (not PS Vue / homebrew) */
  char sd[120]; snprintf(sd, sizeof sd, "/user/home/%s/savedata", u); DIR *g = opendir(sd); struct dirent *f; static char nm[64][32]; int n = 0;
  while (g && (f = readdir(g))) { if (strlen(f->d_name) != 9 || !strcmp(f->d_name, VUE_ID) || r2_homebrew(f->d_name)) continue; if (sv_disk_parts(u, f->d_name, nm, 64)) n++; }
  if (g) closedir(g); return n;
}
static void release_names(void) {   /* after the release clean slate: names, pictures, tags of the released profiles */
  char b[400] = ""; FILE *f = fopen(CLND "/release", "r"); if (!f) return; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); unlink(CLND "/release");
  uint32_t mainu = r2_main_user(); int ren = 0, pic = 0, tags = 0;
  for (char *p = strtok(b, "\n"); p; p = strtok(0, "\n")) { char u[16] = ""; int k = 0; if (sscanf(p, "%15s %d", u, &k) < 1) continue; uint32_t uid = (uint32_t)strtoul(u, 0, 16);
    char op[120]; snprintf(op, sizeof op, "/data/round2/prep/owner_%s.txt", u); if (!unlink(op)) tags++;
    if (uid == mainu || k < 1) continue;   /* the main profile keeps the room's name and picture */
    char want[16], have[64] = ""; snprintf(want, sizeof want, "Player %d", k); sceUserServiceGetUserName((int32_t)uid, have, sizeof have);
    if (strcmp(have, want) && !sceUserServiceSetUserName((int32_t)uid, want)) ren++;
    char s1[48]; snprintf(s1, sizeof s1, "/_avreset.%s", u); int m0 = look_mute; look_mute = 1; look_last[0] = 0; look_avcopy(s1, 1); look_mute = m0; if (!strncmp(look_last, "avatar/result", 13)) pic++; }
  sv_log("switch: released profiles - %d renamed, %d picture(s) reset, %d tag(s) removed", ren, pic, tags);
}
static void look_memplay(const char *key, const char *spec);
static void cloud_step(const char *key, int budget);
static void switch_resume(const char *key) {   /* the release is done: start the member load that was waiting */
  release_names();
  char sp[220] = ""; FILE *f = fopen(MEMAFTER, "r"); if (f) { if (!fgets(sp, sizeof sp, f)) sp[0] = 0; fclose(f); } unlink(MEMAFTER); unlink(MEMQ);
  sp[strcspn(sp, "\r\n")] = 0; if (!sp[0]) { unlink("/data/round2/memqn"); return; }
  sv_log("switch: release done - sending the released saves to the cloud first, then loading: %s", sp);
  { double t0 = tm_now(); cloud_step(key, 120); sv_log("switch: cloud batch before the load took %.0f s", tm_now() - t0); }   /* newest copies up before anyone loads them */
  int m0 = look_mute; look_mute = 1; look_memplay(key, sp); look_mute = m0;
}
static void look_memplay(const char *key, const char *spec) {
  char buf[200], *tok[12]; int nt = 0; snprintf(buf, sizeof buf, "%s", spec + 10);
  for (char *p = buf, *q; p && nt < 12; p = q) { q = strchr(p, '.'); if (q) *q++ = 0; tok[nt++] = p; }
  if (nt < 3 || !(nt % 2) || !mem_ok(tok[0])) { look_kv("memplay/error", "bad spec"); return; }
  { char st[16] = ""; cl_rd("state", st, sizeof st); if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("memplay/waits", "- the room is being reset first"); return; } }
  /* 1.1.97: positions – main -> main profile, the k-th ticked player -> Player k (spares in id order) */
  char posv[6][2]; int k = 0, np = (nt - 1) / 2; char used[5][16]; memset(used, 0, sizeof used);
  for (int i = 0; i < np; i++) { const char *sl = tok[1 + 2 * i];
    if (!strcmp(sl, "main")) snprintf(posv[i], 2, "m"); else { snprintf(posv[i], 2, "%d", k + 1); if (k < 4) snprintf(used[k], 16, "%s", sl); k++; } }
  /* 1.1.124: release the profiles that don't take a ticked player (see above) - once per load (MEMAFTER marks the second pass) */
  if (!file_exists(MEMAFTER ".done")) {
    RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
    uint32_t mainu = r2_main_user(), sp[16]; int ns = 0, mainticked = 0;
    for (int i = 0; i < np; i++) if (!strcmp(tok[1 + 2 * i], "main")) mainticked = 1;
    for (int i = 0; i < 16; i++) { if (L.userId[i] == -1 || !L.userId[i]) continue; if ((uint32_t)L.userId[i] != mainu) sp[ns++] = (uint32_t)L.userId[i]; }
    for (int i = 0; i < ns; i++) for (int j = i + 1; j < ns; j++) if (sp[j] < sp[i]) { uint32_t x = sp[i]; sp[i] = sp[j]; sp[j] = x; }
    char rel[400] = ""; int nrel = 0; mkdir(CLND, 0777); FILE *td = fopen(CLND "/todo", "w"); int ng = 0;
    for (int i = -1; i < ns && i < 4; i++) { uint32_t pu = i < 0 ? mainu : sp[i]; if (!pu) continue;
      if (i < 0 ? mainticked : i < k) continue;   /* takes a ticked player: loaded over (old data kept as a backup first) */
      char u[9]; snprintf(u, sizeof u, "%08x", pu); char op[120], want[16], have[64] = ""; snprintf(op, sizeof op, "/data/round2/prep/owner_%s.txt", u);
      snprintf(want, sizeof want, "Player %d", i + 1); if (i >= 0) sceUserServiceGetUserName((int32_t)pu, have, sizeof have);
      int games = prof_games(u), dirty = games || file_exists(op) || (i >= 0 && strcmp(have, want));
      if (!dirty) continue;
      if (user_signed_in(pu) && pu != mainu) { sceUserServiceLogout((int32_t)pu); sleep(2); }
      size_t l = strlen(rel); snprintf(rel + l, sizeof rel - l, "%s %d\n", u, i < 0 ? 0 : i + 1); nrel++;
      if (games && td) { char sd[120]; snprintf(sd, sizeof sd, "/user/home/%s/savedata", u); DIR *g = opendir(sd); struct dirent *f; static char nm[64][32];
        while (g && (f = readdir(g))) { if (strlen(f->d_name) != 9 || !strcmp(f->d_name, VUE_ID) || r2_homebrew(f->d_name)) continue; if (!sv_disk_parts(u, f->d_name, nm, 64)) continue; fprintf(td, "%s %s\n", u, f->d_name); ng++; }
        if (g) closedir(g); } }
    if (td) fclose(td);
    if (nrel) {
      put_file(CLND "/release", rel); put_file(MEMAFTER, spec); put_file(MEMAFTER ".done", "1");
      { FILE *qf = fopen("/data/round2/memqn", "w"); if (qf) { fprintf(qf, "%d %s\n", np, tok[2]); fclose(qf); } }
      put_file(MEMQ, "release - - -\n");   /* keeps the engine/tile on "Getting <name>'s profile ready..." */
      char v[160]; snprintf(v, sizeof v, "%08x releasing %d other profile(s) first (%d game(s) to clean)", r2_main_user(), nrel, ng); look_kv("memplay/loading", v);
      sv_log("switch: %s for %s", v, spec);
      if (ng) { cl_begin("switch", tok[2]); cl_wr("pos", "0\n"); cl_wr("stat", "0 %d 0 0\n", ng); cl_wr("index", ""); cl_wr("mode", "clean\n"); cl_wr("state", "running\n"); clean_on = 1; }
      else switch_resume(key);
      return;
    }
  }
  unlink(MEMAFTER ".done");
  /* link the whole room to the crew: main -> main, Player k -> the player loaded there, the others -> extra slots x<k> */
  { RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
    uint32_t mainu = r2_main_user(), sp[16]; int ns = 0;
    for (int i = 0; i < 16; i++) { if (L.userId[i] == -1 || !L.userId[i]) continue; if ((uint32_t)L.userId[i] != mainu) sp[ns++] = (uint32_t)L.userId[i]; }
    for (int i = 0; i < ns; i++) for (int j = i + 1; j < ns; j++) if (sp[j] < sp[i]) { uint32_t x = sp[i]; sp[i] = sp[j]; sp[j] = x; }
    mkdir("/data/round2/prep", 0777);
    for (int i = -1; i < ns && i < 4; i++) { uint32_t pu = i < 0 ? mainu : sp[i]; if (!pu) continue; char op[120], tv[80];
      if (i < 0) snprintf(tv, sizeof tv, "%s/main", tok[0]); else if (used[i][0]) snprintf(tv, sizeof tv, "%s/%s", tok[0], used[i]); else snprintf(tv, sizeof tv, "%s/x%d", tok[0], i + 1);
      snprintf(op, sizeof op, "/data/round2/prep/owner_%08x.txt", pu); put_file(op, tv); sv_log("member link: profile %08x -> %s", pu, tv); } }
  unlink(MEMQ);
  { FILE *qf = fopen("/data/round2/memqn", "w"); if (qf) { fprintf(qf, "%d %s\n", np, tok[2]); fclose(qf); } } /* 1.1.101 */
  if (np > 1) { FILE *f = fopen(MEMQ, "w"); if (f) { for (int i = 1; i < np; i++) fprintf(f, "%s %s %s %s\n", tok[0], tok[1 + 2 * i], tok[2 + 2 * i], posv[i]); fclose(f); } }
  char one[160]; snprintf(one, sizeof one, "/_memplay.%s.%s.%s.%s", tok[0], tok[1], tok[2], posv[0]);
  memplay_one(key, one, 1);
  { char st[16] = ""; cl_rd("state", st, sizeof st); if (strcmp(st, "running") && strcmp(st, "paused")) memplay_next(key); } /* first one didn't start a load: go on now */
}
/* 1.1.97 (owner 20 Nov 1:19am: switch trophy pop-ups off on every profile automatically): "/_regscan.<user>" lists every number the
   PS4 keeps for that profile (registry, the profile's own block, read-only). Compare one profile before/after changing a
   setting by hand to find where the setting lives. */
static void look_regscan(const char *spec) {
  char u[16] = "", k[48], v[48]; if (sscanf(spec, "/_regscan.%8[0-9a-f]", u) != 1) { look_kv("regscan/error", "bad spec"); return; }
  long base = prof_slot_base((uint32_t)strtoul(u, 0, 16)); if (!base) { look_kv("regscan/error", "no such profile"); return; }
  int n = 0; for (long off = 0; off < 0x10000 && n < 390; off++) { int x = 0; if (sceRegMgrGetInt(base + off, &x) == 0) { snprintf(k, sizeof k, "rs/%04lx", off); snprintf(v, sizeof v, "%d", x); look_kv(k, v); n++; } }
  snprintf(v, sizeof v, "%d", n); look_kv("regscan/count", v);
}
static void look_copy(const char *spec) {
  char st[16] = ""; cl_rd("state", st, sizeof st);
  if (strcmp(spec, "/_copy.all")) { look_kv("copy/error", "bad spec (use /_copy.all)"); return; }
  if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("copy/refused", "a clean slate / copy is already running"); return; }
  mkdir(CLND, 0777); mkdir("/data/round2/prep", 0777);
  FILE *td = fopen(CLND "/todo", "w"); if (!td) { look_kv("copy/error", "can't write the list"); return; }
  RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
  int ng = 0, nu = 0;
  for (int i = 0; i < 16; i++) {
    if (L.userId[i] == -1 || !L.userId[i]) continue;
    char u[9]; snprintf(u, sizeof u, "%08x", (uint32_t)L.userId[i]); nu++;
    char sd[120]; snprintf(sd, sizeof sd, "/user/home/%s/savedata", u); DIR *g = opendir(sd); struct dirent *f; static char nm[64][32];
    while (g && (f = readdir(g))) {
      if (strlen(f->d_name) != 9 || !strcmp(f->d_name, VUE_ID) || r2_homebrew(f->d_name)) continue;
      if (!sv_disk_parts(u, f->d_name, nm, 64)) continue;
      fprintf(td, "%s %s\n", u, f->d_name); ng++;
    }
    if (g) closedir(g);
  }
  fclose(td);
  cl_begin("copy", "");
  cl_wr("pos", "0\n"); cl_wr("stat", "0 %d 0 0\n", ng); cl_wr("index", ""); cl_wr("mode", "copy\n"); cl_wr("state", ng ? "running\n" : "done\n");
  clean_on = ng > 0;
  sv_log("copy only started: %d profile(s), %d game(s) - nothing will be removed", nu, ng);
  char v[80]; snprintf(v, sizeof v, "%d games on %d profiles (copy only)", ng, nu); look_kv("copy/started", v);
}
/* 1.1.74: "/_copycheck" - does the internal copy match the restore point in the cloud? For every part in the restore point's
   list (snapshot index: files + bytes as uploaded) compare with the internal copy's manifest (files + bytes). Each mismatch
   is written to save.log. */
static void look_copycheck(void) {
  FILE *ix = fopen(SNAPD "/index", "r"); if (!ix) { look_kv("copycheck/error", "no restore point on this PS4"); return; }
  char ln[200], u[16], t[16], nm[48]; int f, fl, same = 0, diff = 0, missing = 0, skipped = 0; unsigned long long by;
  while (fgets(ln, sizeof ln, ix)) {
    if (sscanf(ln, "%15s %15s %47s %d %llu %d", u, t, nm, &f, &by, &fl) != 6) continue;
    if (fl || f <= 0) { skipped++; continue; }
    char mp[300]; snprintf(mp, sizeof mp, "/data/round2/prep/%s/%s/%s/manifest.txt", u, t, nm);
    FILE *m = fopen(mp, "r"); if (!m) { missing++; sv_log("copy check: %s %s %s in the restore point but NO internal copy", u, t, nm); continue; }
    char ml[600]; int mf = 0; unsigned long long mb = 0;
    while (fgets(ml, sizeof ml, m)) { char *a = strchr(ml, '\t'); if (!a) continue; mf++; mb += strtoull(a + 1, 0, 10); }
    fclose(m);
    if (mf == f && mb == by) same++;
    else { diff++; sv_log("copy check: %s %s %s DIFFERS - restore point %d files %llu bytes, internal %d files %llu bytes", u, t, nm, f, by, mf, mb); }
  }
  fclose(ix);
  char v[160]; snprintf(v, sizeof v, "%d same, %d different, %d with no internal copy, %d not in the restore point (failed/empty)", same, diff, missing, skipped);
  look_kv("copycheck/result", v); sv_log("copy check: %s", v);
}
static void look_clean(const char *spec) {
  char st[16] = ""; cl_rd("state", st, sizeof st);
  if (!strcmp(spec, "/_clean.stop")) { if (!strcmp(st, "running") || !strcmp(st, "paused")) { cl_wr("state", "stopped\n"); clean_on = 0; sv_log("clean slate stopped by staff"); look_kv("clean/result", "stopped"); } else look_kv("clean/result", "nothing running"); return; }
  if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("clean/refused", "a clean slate is already running"); return; }
  char lst[200] = ""; if (sscanf(spec, "/_clean.%199[0-9a-f-]", lst) != 1) { look_kv("clean/error", "bad spec"); return; }
  mkdir(CLND, 0777);
  FILE *td = fopen(CLND "/todo", "w"); if (!td) { look_kv("clean/error", "can't write the list"); return; }
  int ng = 0, nu = 0; uint32_t mainu = r2_main_user();
  for (char *t = lst; *t; ) {
    char *e = strchr(t, '-'); size_t len = e ? (size_t)(e - t) : strlen(t); char u[9];
    if (len != 8) { fclose(td); look_kv("clean/error", "bad user id"); return; }
    memcpy(u, t, 8); u[8] = 0; t += len + (e ? 1 : 0);
    uint32_t uid = (uint32_t)strtoul(u, 0, 16);
    if (!prof_slot_base(uid)) { fclose(td); look_kv("clean/error", "no such profile"); return; }
    if (user_signed_in(uid) && uid != mainu) { fclose(td); look_kv("clean/refused", "a listed spare profile is signed in - sign it out first"); return; }
    nu++;
    char sd[120]; snprintf(sd, sizeof sd, "/user/home/%s/savedata", u); DIR *g = opendir(sd); struct dirent *f; static char nm[64][32];
    while (g && (f = readdir(g))) {
      if (strlen(f->d_name) != 9 || !strcmp(f->d_name, VUE_ID) || r2_homebrew(f->d_name)) continue;
      if (r2_protected(f->d_name) && uid == mainu) continue;
      if (!sv_disk_parts(u, f->d_name, nm, 64)) continue;
      fprintf(td, "%s %s\n", u, f->d_name); ng++;
    }
    if (g) closedir(g);
  }
  fclose(td);
  cl_begin("clean", "");
  cl_wr("pos", "0\n"); cl_wr("stat", "0 %d 0 0\n", ng); cl_wr("index", ""); cl_wr("mode", "clean\n"); cl_wr("state", ng ? "running\n" : "done\n");
  clean_on = ng > 0;
  sv_log("clean slate started: %d profile(s), %d game(s)", nu, ng);
  char v[80]; snprintf(v, sizeof v, "%d games on %d profiles", ng, nu); look_kv("clean/started", v);
}
/* ---- 1.1.53 SESSION RESET (owner 17 Nov 10:15pm: the clean slate after each session, tested from 18 Nov). "/_reset" or
   "/_reset.<label>" (the engine sends it after a guest session ends; staff can press "Clean slate now"). In order:
   1) close the game left open (waits while it is still saving, up to 3 min; if it can't close, nothing else happens);
   2) sign out every spare profile still signed in (never the main);
   3) every game on every profile (main included): each part copied to the PS4's own storage with fingerprints (INTERNAL
      FIRST), then removed - only if the whole game copied; never PS Vue, the protected main-profile game, homebrew settings;
   4) profiles: main + the 4 oldest spares kept; spares created by guests deleted (their saves were just copied + removed);
      fewer than 4 spares = error "add by hand"; spares renamed Player 1-4, pictures back to default, order main first;
   5) one line in the clean-slate log (+ every problem in the error log). */
static void reset_profiles(char *out, size_t on) {
  RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
  uint32_t mainu = r2_main_user(), sp[16]; int ns = 0;
  for (int i = 0; i < 16; i++) { if (L.userId[i] == -1 || !L.userId[i]) continue; if ((uint32_t)L.userId[i] != mainu) sp[ns++] = (uint32_t)L.userId[i]; }
  for (int i = 0; i < ns; i++) for (int j = i + 1; j < ns; j++) if (sp[j] < sp[i]) { uint32_t x = sp[i]; sp[i] = sp[j]; sp[j] = x; }
  int del = 0, ren = 0, pic = 0, bad = 0; char s1[64];
  for (int i = 4; i < ns; i++) {
    if (user_signed_in(sp[i])) { sceUserServiceLogout((int32_t)sp[i]); sleep(3); }
    snprintf(s1, sizeof s1, "/_udel.%08x", sp[i]); look_mute = 1; look_last[0] = 0; look_udel(s1); look_mute = 0;
    if (!strncmp(look_last, "udel/result: deleted", 20)) del++; else { bad++; cl_err("extra profile %08x not deleted: %s", sp[i], look_last); }
  }
  int keep = ns < 4 ? ns : 4;
  if (ns < 4) { bad++; cl_err("only %d spare profile(s) - %d to add by hand (Settings > Users), the helper can't create profiles", ns, 4 - ns); }
  for (int i = 0; i < keep; i++) {
    char want[16], have[64] = ""; snprintf(want, sizeof want, "Player %d", i + 1);
    if (user_signed_in(sp[i])) { sceUserServiceLogout((int32_t)sp[i]); sleep(3); if (user_signed_in(sp[i])) { bad++; cl_err("spare %08x would not sign out", sp[i]); } }
    sceUserServiceGetUserName((int32_t)sp[i], have, sizeof have);
    if (strcmp(have, want)) { bpop_key("ren", "Room reset: renaming '%s' back to '%s'", have, want); int r = sceUserServiceSetUserName((int32_t)sp[i], want); if (r == 0) ren++; else { bad++; cl_err("spare %08x '%s' not renamed to %s (0x%08x)", sp[i], have, want, (unsigned)r); } }
    snprintf(s1, sizeof s1, "/_avreset.%08x", sp[i]); look_mute = 1; look_last[0] = 0; look_avcopy(s1, 1); look_mute = 0;
    if (!strncmp(look_last, "avatar/result", 13)) pic++; else { bad++; cl_err("spare %08x picture not reset: %s", sp[i], look_last); }
  }
  char ord[200] = "-";
  if (keep) {
    char sp2[200]; int n = snprintf(sp2, sizeof sp2, "/_uorder.%08x", mainu); for (int i = 0; i < keep; i++) n += snprintf(sp2 + n, sizeof sp2 - n, "-%08x", sp[i]);
    look_mute = 1; look_last[0] = 0; look_uorder(sp2); look_mute = 0;
    if (strstr(look_last, " 0 failed")) snprintf(ord, sizeof ord, "ok"); else { bad++; snprintf(ord, sizeof ord, "FAILED"); cl_err("profile order: %s", look_last); }
  }
  snprintf(out, on, "%d spares%s, %d renamed, %d deleted, order %s", keep, ns < 4 ? " SHORT" : "", ren, del, ord); /* 1.1.54: short - the report keeps 150 chars a line */
  sv_log("reset profiles: %s (%d problem(s))", out, bad);
}
/* 1.1.78 (owner 19 Nov 5:30pm: "can a clean slate disconnect controllers after it is done, so profiles refresh?"): after a
   clean slate / reset / purge, every signed-in profile is signed out and the room's main profile signed back in, so the PS4
   reloads each profile's save list fresh and whoever picks up a controller chooses a profile again. */
/* 1.1.91 (owner 20 Nov 12:23am: after the clean slate signed everyone out and the main profile back in, the controller was
   "frozen" – still tied to a signed-out user, and the PS button only reopened the last app): switch the controllers OFF, like
   holding the PS button 10 s / Quick Menu > Turn Off Device. Next press of the PS button = the normal "who's using this
   controller" screen. Looked up by name in libScePad (scePadDisconnectDevice); "/_padoff" tests it now; refresh_profiles does
   it before signing everyone out once /data/round2/padoff-on exists (switched on after the test). */
static int pad_off(char *out, size_t on) {
  static int (*pinit)(void), (*gh)(int32_t, int32_t, int32_t), (*po)(int32_t, int32_t, int32_t, const void *), (*pc)(int32_t), (*dis)(int32_t); static int looked, pm, inited;
  if (!looked) { looked = 1; pm = sceKernelLoadStartModule("/system/common/lib/libScePad.sprx", 0, 0, 0, 0, 0);
    if (pm > 0) { sceKernelDlsym(pm, "scePadInit", (void **)&pinit); sceKernelDlsym(pm, "scePadGetHandle", (void **)&gh); sceKernelDlsym(pm, "scePadOpen", (void **)&po);
      sceKernelDlsym(pm, "scePadClose", (void **)&pc); sceKernelDlsym(pm, "scePadDisconnectDevice", (void **)&dis); } }
  if (!dis) { snprintf(out, on, "no disconnect function (pad module 0x%08x)", (unsigned)pm); return -1; }
  if (!inited && pinit) { inited = 1; pinit(); }
  UserIdList l; memset(&l, 0xff, sizeof l); sceUserServiceGetLoginUserIdList(&l);
  int n = 0, ok = 0; size_t w = 0; out[0] = 0;
  for (int i = 0; i < 4; i++) { if (l.userId[i] == -1 || !l.userId[i]) continue; n++;
    int h = gh ? gh(l.userId[i], 0, 0) : -1, opened = 0; if (h < 0 && po) { h = po(l.userId[i], 0, 0, 0); opened = h >= 0; }
    int r = h >= 0 ? dis(h) : h; if (h >= 0 && !r) ok++; if (opened && pc) pc(h);
    if (w < on) w += snprintf(out + w, on - w, "%s%08x h%d r0x%08x", w ? ", " : "", (unsigned)l.userId[i], h, (unsigned)r); }
  if (!n) snprintf(out, on, "nobody signed in");
  sv_log("controllers off: %d of %d (%s)", ok, n, out); return ok;
}
static __attribute__((unused)) void look_padoff(void) { char r[240]; int ok = pad_off(r, sizeof r); look_kv(ok > 0 ? "padoff/result" : "padoff/error", r); }
static void refresh_profiles(void) { /* 1.1.92 (owner 20 Nov 12:30am): NO sign-out any more. Signing out the profile a controller is
     using (main or a spare) left that controller "frozen" on Kaeden; the rooms' helper never signed anyone out and never had
     that. The clean slate still empties every profile and resets names; guests switch profiles themselves. */
  sv_log("profiles: nobody signed out (1.1.92)");
}
static void clean_finish(int done, int fails, int parts) {
  char rn[120] = "", kind[16] = "clean", lab[48] = "-", pr[200] = "", cb[48], t0s[24], t1s[24]; long long t0 = 0, cp = 0, cby = 0;
  cl_rd("run", rn, sizeof rn); sscanf(rn, "%15s %lld %47s", kind, &t0, lab);
  if (!strcmp(kind, "reset")) { reset_profiles(pr, sizeof pr); cl_wr("profiles", "%s\n", pr); }
  if (!strcmp(kind, "reset") || !strcmp(kind, "clean") || !strcmp(kind, "purge")) { refresh_profiles(); /* 1.1.78 */
    DIR *pd = opendir("/data/round2/prep"); struct dirent *pe; int nt = 0; /* 1.1.84: profiles no longer belong to a member (their queued copies keep the tag) */
    while (pd && (pe = readdir(pd))) if (!strncmp(pe->d_name, "owner_", 6)) { char op[200]; snprintf(op, sizeof op, "/data/round2/prep/%s", pe->d_name); if (!unlink(op)) nt++; }
    if (pd) closedir(pd); if (nt) sv_log("member tags cleared: %d profile(s)", nt); }
  cl_rd("copied", cb, sizeof cb); sscanf(cb, "%lld %lld", &cp, &cby);
  char eb[16]; cl_rd("errn", eb, sizeof eb); int en = atoi(eb);
  cl_stamp(t0s, sizeof t0s, t0 ? t0 : (long long)time(0)); cl_stamp(t1s, sizeof t1s, (long long)time(0));
  char line[600]; snprintf(line, sizeof line, "%s-%s %s %s: %s · %d games, %d failed, %d removed, %lld copied %lldMB%s%s · %d problems",
    t0s, t1s + 6, kind, lab, en || fails ? "PROBLEMS" : "OK", done, fails, parts, cp, cby / 1048576, pr[0] ? " · " : "", pr, en);
  cl_hist(line); sv_log("clean log: %s", line);
}
static void reset_go(const char *lab) {
  char st[16] = "", v[220]; cl_rd("state", st, sizeof st);
  if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("reset/refused", "a clean slate is already running"); return; }
  mkdir(CLND, 0777); cl_begin("reset", lab);
  char t[32]; running_title(t, sizeof t);
  if (t[0]) { char res[160]; if (close_game(res, sizeof res)) { cl_err("reset NOT started: the game could not be closed (%s)", res); char ts[24], line[300]; cl_stamp(ts, sizeof ts, (long long)time(0));
      snprintf(line, sizeof line, "%s reset %s: NOT DONE - game still open (%s)", ts, lab[0] ? lab : "-", res); cl_hist(line); look_kv("reset/refused", res); return; }
    snprintf(v, sizeof v, "closed: %s", res); look_kv("reset/close", v); }
  RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
  uint32_t mainu = r2_main_user(); int so = 0;
  for (int i = 0; i < 16; i++) { if (L.userId[i] == -1 || !L.userId[i] || (uint32_t)L.userId[i] == mainu) continue; if (user_signed_in((uint32_t)L.userId[i])) { sceUserServiceLogout(L.userId[i]); so++; } }
  if (so) sleep(3);
  FILE *td = fopen(CLND "/todo", "w"); if (!td) { cl_err("reset: can't write the list"); look_kv("reset/error", "can't write the list"); return; }
  int ng = 0, nu = 0, skip = 0; static char nm[64][32];
  for (int i = 0; i < 16; i++) {
    if (L.userId[i] == -1 || !L.userId[i]) continue;
    char u[9]; snprintf(u, sizeof u, "%08x", (uint32_t)L.userId[i]); nu++;
    char sd[120]; snprintf(sd, sizeof sd, "/user/home/%s/savedata", u); DIR *g = opendir(sd); struct dirent *f;
    while (g && (f = readdir(g))) {
      if (strlen(f->d_name) != 9) continue;
      if (!strcmp(f->d_name, VUE_ID) || r2_homebrew(f->d_name) || (r2_protected(f->d_name) && (uint32_t)L.userId[i] == mainu)) { skip++; continue; }
      if (!sv_disk_parts(u, f->d_name, nm, 64)) continue;
      fprintf(td, "%s %s\n", u, f->d_name); ng++;
    }
    if (g) closedir(g);
  }
  fclose(td);
  mkdir("/data/round2/prep", 0777);
  cl_wr("pos", "0\n"); cl_wr("stat", "0 %d 0 0\n", ng); cl_wr("index", ""); cl_wr("mode", "reset %lld\n", (long long)time(0)); cl_wr("state", "running\n");
  clean_on = 1; /* even with no games: the profile step still runs */
  sv_log("session reset started (%s): %d profile(s), %d game(s), %d kept, %d spare(s) signed out", lab[0] ? lab : "-", nu, ng, skip, so);
  snprintf(v, sizeof v, "%d games on %d profiles, %d signed out", ng, nu, so); look_kv("reset/started", v);
}
/* ---- 1.1.55 COUNTDOWN + FAILSAFES (owner 17 Nov 11:16pm: pop-ups 5 min, 1 min, 30 s and 10 s before; "executing it correctly
   is the hardest part - suggest some failsafes"). "/_reset.<label>.w<seconds>" (the engine sends it 5 min before the reset
   time) starts a countdown instead of resetting at once: PS4 pop-ups (they show over a running game) at the start, 60, 30 and
   10 s. 20 s before the end the helper asks the engine (/ps4/resetok) - the engine looks at SimplyBook LIVE: room still in
   use (extension, next guest), automatic reset switched off, or no answer at all = NOT reset (when in doubt, nothing is
   closed or cleared). "/_reset.cancel" (staff "Stop reset", or the engine when it sees an extension) stops it. The countdown
   runs on the helper's own timer, so it keeps going while a game is open, and survives a helper restart (clean/countdown). */
static int cd_mask; /* cd_on, cd_ok, cd_deadline, cd_lab: declared at the top (the report uses them) */
/* 1.1.62 (owner 18 Nov 6:01pm: no always-on detector): the round2_input plugin sleeps unless /data/round2/input/arm holds
   a future time. Written while a countdown runs (until = deadline + 61 min, never more than 2 h ahead - the plugin ignores
   anything further), deleted when it ends. A stale file runs out by itself. */
static void cd_arm(void) {
  if (!cd_on) { unlink("/data/round2/input/arm"); return; }
  long long now = (long long)time(0), until = cd_deadline + 3660; if (until > now + 7000) until = now + 7000; if (until <= now) until = now + 60;
  mkdir("/data/round2", 0777); mkdir("/data/round2/input", 0777); chmod("/data/round2", 0777); chmod("/data/round2/input", 0777);
  char b[24]; int n = snprintf(b, sizeof b, "%lld\n", until); if (n <= 0 || n >= (int)sizeof b) return;
  int fd = open("/data/round2/input/arm.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0666); if (fd < 0) return;
  int w = (int)write(fd, b, (size_t)n); close(fd);
  if (w != n || rename("/data/round2/input/arm.tmp", "/data/round2/input/arm")) unlink("/data/round2/input/arm.tmp");
}
static void cd_save(void) { if (cd_on) cl_wr("countdown", "%lld %d %s\n", cd_deadline, cd_mask, cd_lab); else unlink(CLND "/countdown"); cd_arm(); }
static void cd_load(void) { char b[120] = ""; if (cl_rd("countdown", b, sizeof b)) return; long long d = 0; int m = 0; char l[48] = "";
  if (sscanf(b, "%lld %d %47s", &d, &m, l) >= 2 && d > (long long)time(0) - 60) { cd_on = 1; cd_deadline = d; cd_mask = m; snprintf(cd_lab, sizeof cd_lab, "%s", l); sv_log("countdown carries on after a restart: %lld s left", d - (long long)time(0)); }
  else unlink(CLND "/countdown"); cd_arm(); }
static void cd_stop(const char *why, int hold, const char *popup) {
  char ts[24], line[300]; cl_stamp(ts, sizeof ts, (long long)time(0));
  snprintf(line, sizeof line, "%s reset %s: %s · %s", ts, cd_lab[0] ? cd_lab : "-", hold ? "HELD" : "NOT DONE", why); cl_hist(line);
  if (!hold) cl_err("reset %s not done: %s", cd_lab, why);
  sv_log("countdown stopped (%s): %s", hold ? "held" : "not done", why);
  if (popup) notify(popup);
  cd_on = 0; cd_ok = 0; cd_play_from = 0; cd_save();
}
static int reset_confirm(const char *key, char *why, size_t wn) { /* 1 = go, 0 = hold (why), -1 = no answer */
  char hdr[200], body[160], out[400], err[120];
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  int n = snprintf(body, sizeof body, "{\"id\":\"%s\",\"lab\":\"%s\"}", console_id, cd_lab);
  for (int i = 0; i < 2; i++) {
    out[0] = 0; int st = r2tls_request(ENGINE_HOST, "POST", "/ps4/resetok", hdr, body, n, out, sizeof out, err, sizeof err);
    if (st == 200) { if (strstr(out, "\"ok\":true")) return 1; char h[160] = ""; jget(out, "hold", h, sizeof h); snprintf(why, wn, "%s", h[0] ? h : "the engine said no"); return 0; }
    sleep(2);
  }
  snprintf(why, wn, "no answer from Round2 - not reset (nothing closed or cleared)"); return -1;
}
/* ---- 1.1.56 BUTTON DETECTOR (owner 17 Nov 11:26pm: "check if any button input is being made for 2 mins, if there are button
   inputs then pause and restart the 2 min timer ... if a game keeps input highly likely guests would extend"). Reads each
   signed-in profile's controller (scePadReadState: buttons, sticks, triggers) and the pad's idle counter (scePadGetIdleCount)
   every 2 s during a reset countdown. Not proven yet that a background program sees the pad while a game has it - if nothing
   is readable the countdown simply runs on its timer as in 1.1.55 (never less safe). "/_input.<seconds>" = read-only test:
   counts changes while someone presses buttons. */
/* 1.1.58 (18 Nov 12:08am): the test froze Max right after it started reading the controller -> ALL controller reading is
   switched off and libScePad is no longer linked. The countdown runs on its timer only (silent), as in 1.1.55. */
#define R2_NO_PAD 1
static int scePadInit(void) { return -1; } static int scePadOpen(int32_t a, int32_t b, int32_t c, const void *d) { (void)a; (void)b; (void)c; (void)d; return -1; }
static int scePadGetHandle(int32_t a, int32_t b, int32_t c) { (void)a; (void)b; (void)c; return -1; }
static int scePadReadState(int32_t a, void *b) { (void)a; (void)b; return -1; } static int scePadGetIdleCount(int32_t a, void *b) { (void)a; (void)b; return -1; }
static int in_init, in_h[4] = { -1, -1, -1, -1 }, in_have[4], in_idl_ok[4]; static int32_t in_u[4]; static unsigned char in_last[4][12]; static uint32_t in_idl[4];
static long long in_scan;
static void in_open(void) {
  if (R2_NO_PAD) return;
  if (!in_init) { in_init = 1; int r = scePadInit(); sv_log("input: scePadInit -> 0x%08x", (unsigned)r); }
  if ((long long)time(0) - in_scan < 30) return; in_scan = (long long)time(0);
  UserIdList l; memset(&l, 0xff, sizeof l); sceUserServiceGetLoginUserIdList(&l);
  for (int i = 0; i < 4; i++) {
    int32_t u = l.userId[i]; if (u == -1 || u == 0) { in_h[i] = -1; in_u[i] = 0; continue; }
    if (in_u[i] == u && in_h[i] >= 0) continue;
    int h = scePadGetHandle(u, 0, 0), h2 = 0; if (h < 0) { h2 = h; h = scePadOpen(u, 0, 0, 0); }
    in_h[i] = h; in_u[i] = u; in_have[i] = 0; in_idl_ok[i] = 0;
    sv_log("input: profile %08x pad handle %d (get 0x%08x, open 0x%08x)", (uint32_t)u, h, (unsigned)h2, (unsigned)h);
  }
}
/* 1.1.62 (18 Nov 2026, owner: "do the controller detector again ... account for overflows, it must work perfectly"):
   the helper NEVER touches the controller. The round2_input GoldHEN plugin runs INSIDE the game (hooks the game's own
   scePadRead/ReadState, watch-only) and writes one line to /data/round2/input/last:
   "r2in 1 <last_input_epoch> <now_epoch> <titleid> <pid> <reads> <active>". The helper only reads that file.
   Unknown (-1) = no plugin file, file older than 20 s, file for another game than the open one, or off switch → the
   countdown runs on its timer only (exactly as before - never less safe). */
#define INP_DIR "/data/round2/input"
#define INP_LAST INP_DIR "/last"
#define INP_OFF INP_DIR "/off"
typedef struct { long long last, now; char title[16]; int pid; unsigned reads, active; } inp_t;
static long long inp_prev_last = -1;
static int inp_read(inp_t *o) {
  memset(o, 0, sizeof *o);
  int fd = open(INP_LAST, O_RDONLY); if (fd < 0) return -1;
  char line[200]; ssize_t n = read(fd, line, sizeof line - 1); close(fd);
  if (n <= 0) return -1; line[n] = 0;
  char t[16] = { 0 }; long long a = 0, b = 0; int pid = 0, v = 0; unsigned r = 0, ac = 0;
  if (sscanf(line, "r2in %d %lld %lld %15s %d %u %u", &v, &a, &b, t, &pid, &r, &ac) != 7 || v != 1) return -1;
  if (a < 0 || b <= 0) return -1;
  o->last = a; o->now = b; snprintf(o->title, sizeof o->title, "%s", t); o->pid = pid; o->reads = r; o->active = ac;
  return 0;
}
static int inp_activity(char *why, size_t wn) { /* 1 = new press/hold since the last call, 0 = quiet, -1 = not known */
  inp_t s; long long now = (long long)time(0); char w[160] = "";
  int res = -1;
  if (file_exists(INP_OFF)) snprintf(w, sizeof w, "plugin switched off");
  else if (inp_read(&s)) snprintf(w, sizeof w, "no plugin file (no game with the plugin open yet)");
  else if (s.now < now - 20 || s.now > now + 20) snprintf(w, sizeof w, "plugin file %lld s old (game closed?)", now - s.now);
  else {
    char rt[32]; running_title(rt, sizeof rt);
    if (!rt[0] || strcmp(rt, s.title)) snprintf(w, sizeof w, "plugin file is for %s, open now: %s", s.title, rt[0] ? rt : "nothing");
    else {
      res = 0;
      if (inp_prev_last >= 0 && s.last > inp_prev_last) res = 1;
      if (inp_prev_last < 0 && s.last > 0 && now - s.last <= 5) res = 1;
      inp_prev_last = s.last;
      if (s.last > 0) snprintf(w, sizeof w, "%s: last press %lld s ago (%u pad reads seen)", s.title, now - s.last, s.reads);
      else snprintf(w, sizeof w, "%s: no press yet since the game opened (%u pad reads seen)", s.title, s.reads);
    }
  }
  if (res < 0) inp_prev_last = -1;
  if (why && wn) snprintf(why, wn, "%s", w);
  return res;
}
/* 1.1.64 (owner 18 Nov 6:22pm): TEST-PHASE pop-ups on the PS4 screen while we try the detector. Only while the file
   /data/round2/input/popups exists ("/_inplug.pop.on" / ".pop.off"); off = silent as before. At most one every 4 s. */
#define INP_POP INP_DIR "/popups"
/* 1.1.65 (18 Nov 6:32pm: SF6 started with the plugin listed but no "loaded" file appeared): games run as a different,
   limited user - the folder the helper made (0755 after umask) may be read-only for them. Open it up (folder only holds
   our tiny status files). */
static void inp_dir_open(void) {
  mkdir("/data/round2", 0777); mkdir(INP_DIR, 0777);
  chmod("/data/round2", 0777); chmod(INP_DIR, 0777);
}
static void inp_pop(const char *fmt, ...) {
  if (!file_exists(INP_POP)) return;
  static long long lastp; long long now = (long long)time(0); if (now - lastp < 4) return; lastp = now;
  char b[110]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  char m[130]; snprintf(m, sizeof m, "Round2 test: %s", b); notify(m); sv_log("popup: %s", m);
}
static void inp_watch(void) { /* every 2 s, only with test pop-ups on: plugin loaded into a game / woke / slept */
  if (!file_exists(INP_POP)) return;
  static int opened; if (!opened) { opened = 1; inp_dir_open(); }
  static long long seen_m; static int seen_pid, was_armed = -1;
  struct stat st;
  if (!stat(INP_LAST, &st) && (long long)st.st_mtime != seen_m) {
    seen_m = (long long)st.st_mtime; inp_t q;
    if (!inp_read(&q) && q.pid != seen_pid) { seen_pid = q.pid; inp_pop("detector loaded in %s (asleep until a reset countdown)", q.title); }
  }
  int armed = file_exists(INP_DIR "/arm");
  if (was_armed >= 0 && armed != was_armed) inp_pop(armed ? "detector AWAKE - watching the controller" : "detector asleep again");
  was_armed = armed;
}
static int in_activity(char *dbg, size_t dn) { /* 1 = someone pressed/moved something since the last call, 0 = quiet, -1 = nothing readable */
  int readable = 0, act = 0; if (dbg) dbg[0] = 0;
  if (R2_NO_PAD) return inp_activity(dbg, dbg ? dn : 0);
  for (int i = 0; i < 4; i++) {
    if (in_h[i] < 0) continue;
    unsigned char d[256]; memset(d, 0, sizeof d); int r = scePadReadState(in_h[i], d);
    if (r == 0) { readable = 1;
      if (in_have[i]) { int ch = memcmp(d, in_last[i], 4) != 0; for (int k = 4; k < 10; k++) if (abs((int)d[k] - (int)in_last[i][k]) > 24) ch = 1; if (ch) act = 1; }
      memcpy(in_last[i], d, 12); in_have[i] = 1; }
    uint32_t ic[8]; memset(ic, 0, sizeof ic); int r2 = scePadGetIdleCount(in_h[i], ic);
    if (r2 == 0) { readable = 1; if (in_idl_ok[i] && ic[0] < in_idl[i]) act = 1; in_idl[i] = ic[0]; in_idl_ok[i] = 1; }
    if (dbg) { size_t l = strlen(dbg); snprintf(dbg + l, dn - l, "%s%08x:read 0x%x btn %02x%02x%02x%02x sticks %d,%d,%d,%d idle 0x%x=%u", l ? " | " : "", (uint32_t)in_u[i], (unsigned)r, d[3], d[2], d[1], d[0], d[4], d[5], d[6], d[7], (unsigned)r2, ic[0]); }
  }
  return readable ? act : -1;
}
static void look_input(const char *spec) { /* "/_input.<seconds>" read-only test: press buttons during it */
  int secs = atoi_safe(spec + 8); if (secs < 5) secs = 5; if (secs > 60) secs = 60;
  in_scan = 0; in_open(); inp_prev_last = -1; /* 1.1.62: reads the plugin's file, never the controller */
  if (!cd_on) { /* wake the plugin just for this test (it sleeps outside countdowns) */
    mkdir("/data/round2", 0777); mkdir(INP_DIR, 0777); char ab[24]; int an = snprintf(ab, sizeof ab, "%lld\n", (long long)time(0) + secs + 15);
    int afd = open(INP_DIR "/arm", O_WRONLY | O_CREAT | O_TRUNC, 0666); if (afd >= 0) { if (write(afd, ab, (size_t)an) != an) {} close(afd); }
    sleep(3); /* the plugin looks for the arm file every 2 s */ }
  inp_pop("input test %d s - press buttons now", secs); char dbg[600] = "", first[600] = ""; int changes = 0, reads = 0, unread = 0;
  in_activity(first, sizeof first);
  for (int t = 0; t < secs * 4; t++) { usleep(250000); int a = in_activity(t == secs * 4 - 1 ? dbg : 0, sizeof dbg); if (a < 0) unread++; else { reads++; if (a) changes++; } }
  char v[120]; snprintf(v, sizeof v, "%d changes in %d s (%d readable samples, %d not readable)", changes, secs, reads, unread);
  if (!cd_on) unlink(INP_DIR "/arm");
  inp_pop("input test done: %d changes, %d readable", changes, reads);
  look_kv("input/result", v); look_kv("input/first", first); look_kv("input/last", dbg);
  sv_log("input test: %s", v); sv_log_long("input test first: ", first); sv_log_long("input test last: ", dbg);
}
/* 1.1.62: install / switch / check the round2_input plugin ("/_inplug.status", "/_inplug.get.<sha256>",
   "/_inplug.on.<CUSA12345|default>", "/_inplug.off"). Safety: the file must match the SHA-256 given in the job AND the
   engine's, size 1 KB-512 KB, PS4 program header; written to a temp file then renamed; plugins.ini is read whole (max 32 KB,
   refused if bigger), backed up (first original kept for good), edited line by line keeping every other line as it was,
   and written temp+rename. "off" = instant stop (off file) + our lines removed from plugins.ini. */
#define GH_PLUG "/data/GoldHEN/plugins/round2_input.prx"
#define GH_PLUG_LINE "/data/GoldHEN/plugins/round2_input.prx"
#define GH_INI "/data/GoldHEN/plugins.ini"
#define PLUG_MIN 1024
#define PLUG_MAX (512 * 1024)
#define INI_MAX (32 * 1024)
static const char *look_key = "";
static int read_small(const char *p, char *buf, size_t cap, size_t *len) { /* whole file or fail; buf gets a NUL */
  *len = 0; buf[0] = 0;
  int fd = open(p, O_RDONLY); if (fd < 0) return -1;
  size_t n = 0; for (;;) {
    if (n >= cap - 1) { char x; ssize_t r = read(fd, &x, 1); close(fd); if (r == 0) { buf[n] = 0; *len = n; return 0; } return r < 0 ? -1 : -2; } /* 1.1.63: full buffer is only "too big" if more data follows */
    ssize_t r = read(fd, buf + n, cap - 1 - n); if (r < 0) { close(fd); return -1; } if (!r) break; n += (size_t)r; }
  close(fd); buf[n] = 0; *len = n; return 0;
}
static int write_atomic(const char *p, const char *d, size_t n) {
  char tmp[200]; snprintf(tmp, sizeof tmp, "%s.r2tmp", p);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (fd < 0) return -1;
  size_t w = 0; while (w < n) { ssize_t r = write(fd, d + w, n - w); if (r <= 0) { close(fd); unlink(tmp); return -1; } w += (size_t)r; }
  fsync(fd); close(fd);
  if (rename(tmp, p)) { unlink(tmp); return -1; }
  return 0;
}
static int plug_sha(char *hex, size_t hn, long *sz) { /* SHA-256 of the installed plugin */
  hex[0] = 0; *sz = -1; struct stat st; if (stat(GH_PLUG, &st) || st.st_size < PLUG_MIN || st.st_size > PLUG_MAX) { if (!stat(GH_PLUG, &st)) *sz = (long)st.st_size; return -1; }
  unsigned char *b = malloc((size_t)st.st_size + 1); if (!b) return -1;
  size_t len = 0; int r = read_small(GH_PLUG, (char *)b, (size_t)st.st_size + 1, &len);
  if (r) { free(b); return -1; }
  unsigned char h[32]; mbedtls_sha256(b, len, h, 0); free(b); *sz = (long)len;
  if (hn < 65) return -1; hexs(h, 32, hex); return 0;
}
static int ini_valid_title(const char *t) { /* "default" or CUSA/CUSX/PPSA-style 4 letters + 5 digits */
  if (!strcmp(t, "default")) return 1;
  if (strlen(t) != 9) return 0;
  for (int i = 0; i < 4; i++) if (t[i] < 'A' || t[i] > 'Z') return 0;
  for (int i = 4; i < 9; i++) if (t[i] < '0' || t[i] > '9') return 0;
  return 1;
}
/* rebuild plugins.ini: drop every line that loads our plugin; if sect != NULL, add our line under [sect] (creating it) */
static int ini_edit(const char *sect, char *msg, size_t mn) {
  static char in[INI_MAX + 1], out[INI_MAX + 512];
  size_t len = 0; int r = read_small(GH_INI, in, sizeof in, &len);
  if (r == -2) { snprintf(msg, mn, "plugins.ini is bigger than 32 KB - not touched"); return -1; }
  if (r == -1) { in[0] = 0; len = 0; }          /* no file yet: start one */
  if (len && !file_exists("/data/round2/plugins.ini.orig")) write_atomic("/data/round2/plugins.ini.orig", in, len);
  if (len) write_atomic("/data/round2/plugins.ini.prev", in, len);
  int noaio = sect && !strcmp(sect, "!noaio"); if (noaio) sect = NULL; /* 1.1.63: only comment out aio_fix lines (owner's OK), everything else as it was */
  size_t o = 0; int in_sect = 0, added = 0, removed = 0, disabled = 0;
  char hdr[24] = ""; if (sect) snprintf(hdr, sizeof hdr, "[%s]", sect);
  char *p = in;
  while (*p) {
    char *e = strchr(p, '\n'); size_t ll = e ? (size_t)(e - p) : strlen(p);
    char line[512]; size_t cl = ll < sizeof line - 1 ? ll : sizeof line - 1; memcpy(line, p, cl); line[cl] = 0;
    if (cl && line[cl - 1] == '\r') line[--cl] = 0;
    char *t = line; while (*t == ' ' || *t == '\t') t++;
    int ours = strstr(t, "round2_input.prx") != NULL && t[0] != ';' && t[0] != '#';
    int aio = noaio && strstr(t, "aio_fix") != NULL && t[0] != ';' && t[0] != '#' && t[0] != '[';
    if (aio) { if (o + ll + 3 >= sizeof out) { snprintf(msg, mn, "plugins.ini too long to rebuild - not touched"); return -1; } out[o++] = ';'; memcpy(out + o, p, ll); o += ll; out[o++] = '\n'; disabled++; }
    else if (ours) { if (noaio) { if (o + ll + 2 >= sizeof out) return -1; memcpy(out + o, p, ll); o += ll; out[o++] = '\n'; } else removed++; }
    else {
      if (o + ll + 2 >= sizeof out) { snprintf(msg, mn, "plugins.ini too long to rebuild - not touched"); return -1; }
      memcpy(out + o, p, ll); o += ll; out[o++] = '\n';
    }
    if (t[0] == '[') {
      in_sect = sect && !strncmp(t, hdr, strlen(hdr));
      if (in_sect && !added) {
        int n = snprintf(out + o, sizeof out - o, "%s\n", GH_PLUG_LINE); if (n <= 0 || (size_t)n >= sizeof out - o) return -1; o += (size_t)n; added = 1;
      }
    }
    p = e ? e + 1 : p + ll;
  }
  if (sect && !added) {
    int n = snprintf(out + o, sizeof out - o, "%s%s\n%s\n", (o && out[o - 1] != '\n') ? "\n" : "", hdr, GH_PLUG_LINE);
    if (n <= 0 || (size_t)n >= sizeof out - o) { snprintf(msg, mn, "no room to add the line"); return -1; }
    o += (size_t)n; added = 1;
  }
  if (write_atomic(GH_INI, out, o)) { snprintf(msg, mn, "couldn't write plugins.ini"); return -1; }
  if (noaio) snprintf(msg, mn, "plugins.ini: %d aio_fix line(s) switched off (backup in /data/round2/plugins.ini.prev)", disabled);
  else snprintf(msg, mn, "plugins.ini: %d old line(s) of ours removed%s%s%s", removed, sect ? ", added under [" : "", sect ? sect : "", sect ? "]" : "");
  return 0;
}
static void inplug_status(void) {
  char hex[65], v[240]; long sz = -1;
  int ok = !plug_sha(hex, sizeof hex, &sz);
  { struct stat pm; if (ok && !stat(GH_PLUG, &pm) && (pm.st_mode & 0111) != 0111) chmod(GH_PLUG, 0755); /* 1.1.67: fix files saved by 1.1.62-66 */
    char mv[60]; snprintf(mv, sizeof mv, "%o", stat(GH_PLUG, &pm) ? 0u : (unsigned)(pm.st_mode & 07777)); look_kv("inplug/file mode", mv); }
  snprintf(v, sizeof v, "%s%s%s (%ld bytes)", ok ? "installed, SHA-256 " : "not installed", ok ? hex : "", ok ? "" : "", sz);
  look_kv("inplug/file", v);
  static char ini[INI_MAX + 1]; size_t len = 0; int r = read_small(GH_INI, ini, sizeof ini, &len);
  if (r) look_kv("inplug/ini", r == -2 ? "plugins.ini bigger than 32 KB" : "no plugins.ini");
  else {
    char cur[24] = "-", list[400] = ""; size_t ln = 0; int n = 0;
    for (char *p = ini; *p; ) {
      char *e = strchr(p, '\n'); size_t ll = e ? (size_t)(e - p) : strlen(p);
      char line[256]; size_t cl = ll < sizeof line - 1 ? ll : sizeof line - 1; memcpy(line, p, cl); line[cl] = 0;
      char *t = line; while (*t == ' ' || *t == '\t') t++;
      if (t[0] == '[') { snprintf(cur, sizeof cur, "%.22s", t); char *x = strchr(cur, ']'); if (x) x[1] = 0; }
      else if (t[0] && t[0] != ';' && t[0] != '#') { n++; if (ln < sizeof list - 1) { int w = snprintf(list + ln, sizeof list - ln, "%s%s %.60s", ln ? " | " : "", cur, t); if (w > 0) ln += (size_t)w < sizeof list - ln ? (size_t)w : sizeof list - ln - 1; } }
      p = e ? e + 1 : p + ll;
    }
    char k[40]; snprintf(k, sizeof k, "inplug/ini (%d lines)", n); look_kv(k, list[0] ? list : "(empty)");
  }
  look_kv("inplug/off", file_exists(INP_OFF) ? "OFF switch set" : "not set");
  look_kv("inplug/popups", file_exists(INP_POP) ? "ON (test phase)" : "off");
  { struct stat a, b, c2; char v2[160]; snprintf(v2, sizeof v2, "input dir %o uid %d | round2 %o uid %d | GoldHEN %o uid %d",
      stat(INP_DIR, &a) ? 0 : (unsigned)(a.st_mode & 07777), stat(INP_DIR, &a) ? -1 : (int)a.st_uid,
      stat("/data/round2", &b) ? 0 : (unsigned)(b.st_mode & 07777), stat("/data/round2", &b) ? -1 : (int)b.st_uid,
      stat("/data/GoldHEN", &c2) ? 0 : (unsigned)(c2.st_mode & 07777), stat("/data/GoldHEN", &c2) ? -1 : (int)c2.st_uid);
    look_kv("inplug/perms", v2); }
  { static char cf[8192]; size_t cl = 0; const char *v = "unknown (no config.ini)"; if (!read_small("/data/GoldHEN/config.ini", cf, sizeof cf, &cl)) { char *q = strstr(cf, "PluginLoader_Enabled"); v = !q ? "not in config.ini" : strchr(q, '1') && strchr(q, '1') < strchr(q, '\n') ? "ON" : "OFF (switch on in the GoldHEN menu)"; } look_kv("inplug/goldhen loader", v); }
  { struct stat ast; char av[64] = "asleep (no arm file)"; static char ab[32]; size_t al = 0; if (!stat(INP_DIR "/arm", &ast) && !read_small(INP_DIR "/arm", ab, sizeof ab, &al)) snprintf(av, sizeof av, "awake until %lld (in %lld s)", atoll(ab), atoll(ab) - (long long)time(0)); look_kv("inplug/arm", av); }
  char last[200]; size_t ll = 0; if (!read_small(INP_LAST, last, sizeof last, &ll)) { char *nl = strchr(last, '\n'); if (nl) *nl = 0; look_kv("inplug/last", last); } else look_kv("inplug/last", "(no file yet)");
  char w[160]; int a = inp_activity(w, sizeof w); char k2[24]; snprintf(k2, sizeof k2, "inplug/now (%d)", a); look_kv(k2, w);
}
/* 1.1.66 (18 Nov 7:04pm: SF6 + plugin listed + loader ON, still no "loaded" file): read the PS4 system log for N seconds
   ("/_klog.<secs>", 3-40) while someone starts the game, keeping lines about plugins / GoldHEN / round2 / errors.
   Source: GoldHEN's log server (127.0.0.1:3232) if it runs, else /dev/klog. Everything bounded: 300-char lines, 60 kept. */
static int klog_keep(const char *l) {
  static const char *w[] = { "lugin", "GoldHEN", "round2", "prx", "PRX", "rror", "ERROR", "fail", "CUSA", 0 };
  for (int i = 0; w[i]; i++) if (strstr(l, w[i])) return 1;
  return 0;
}
static void look_klog(const char *spec) {
  int secs = atoi_safe(spec + 7); if (secs < 3) secs = 3; if (secs > 40) secs = 40;
  int fd = -1; const char *src = "none";
  int sk = socket(AF_INET, SOCK_STREAM, 0);
  if (sk >= 0) { struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(3232); a.sin_addr.s_addr = htonl(0x7f000001);
    if (!connect(sk, (struct sockaddr *)&a, sizeof a)) { fd = sk; src = "GoldHEN log server :3232"; } else close(sk); }
  if (fd < 0) { fd = open("/dev/klog", O_RDONLY | O_NONBLOCK); if (fd >= 0) src = "/dev/klog"; }
  if (fd < 0) { char e[80]; snprintf(e, sizeof e, "can't open the system log (errno %d)", errno); look_kv("klog/error", e); return; }
  if (fd == sk) { struct timeval tv = { 0, 300000 }; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); }
  look_kv("klog/source", src);
  inp_pop("reading the system log for %d s - start the game now", secs);
  char buf[2048], line[301]; size_t ll = 0; int kept = 0, total = 0, dropped = 0;
  long long end = (long long)time(0) + secs;
  while ((long long)time(0) < end) {
    ssize_t n = read(fd, buf, sizeof buf);
    if (n <= 0) { usleep(200000); continue; }
    for (ssize_t i = 0; i < n; i++) {
      char ch = buf[i];
      if (ch == '\n' || ll >= sizeof line - 1) {
        line[ll] = 0; total++;
        if (ll && klog_keep(line)) { if (kept < 60) { char k[32]; snprintf(k, sizeof k, "tail/klog %02d", ++kept); look_kv(k, line); } else dropped++; }
        ll = 0; if (ch == '\n') continue;
      }
      if (ch == '\r' || ch == 0) continue;
      line[ll++] = (ch >= 32 && ch < 127) ? ch : '?';
    }
  }
  close(fd);
  char v[120]; snprintf(v, sizeof v, "%d lines read in %d s, %d kept, %d more not kept", total, secs, kept, dropped); look_kv("klog/result", v);
  inp_pop("system log read: %d lines, %d about plugins", total, kept);
}
static void look_inplug(const char *spec) {
  char msg[200] = "";
  if (!strcmp(spec, "/_inplug.status")) { inplug_status(); return; }
  if (!strcmp(spec, "/_inplug.off")) {
    mkdir("/data/round2", 0777); mkdir(INP_DIR, 0777);
    int a = write_atomic(INP_OFF, "1", 1);
    int b = file_exists(GH_INI) ? ini_edit(NULL, msg, sizeof msg) : 0;
    sv_log("plugin round2_input switched OFF (%s; %s)", a ? "off file FAILED" : "off file set", b ? msg : (msg[0] ? msg : "no plugins.ini"));
    look_kv("inplug/result", a ? "couldn't set the off switch" : "off - the plugin stops within 2 s; removed from plugins.ini for the next game start"); inp_pop("detector switched OFF");
    if (msg[0]) look_kv("inplug/ini", msg);
    inplug_status(); return;
  }
  if (!strcmp(spec, "/_inplug.pop.on") || !strcmp(spec, "/_inplug.pop.off")) {
    int on = spec[13] == 'o' && spec[14] == 'n';
    if (on) { inp_dir_open(); write_atomic(INP_POP, "1", 1); notify("Round2 test: detector pop-ups ON"); }
    else unlink(INP_POP);
    sv_log("detector test pop-ups %s", on ? "ON" : "OFF"); look_kv("inplug/popups", on ? "ON" : "OFF"); return;
  }
  if (!strcmp(spec, "/_inplug.noaio")) {
    if (!file_exists(GH_INI)) { look_kv("inplug/error", "no plugins.ini"); return; }
    if (ini_edit("!noaio", msg, sizeof msg)) { look_kv("inplug/error", msg); return; }
    sv_log("%s", msg); look_kv("inplug/result", msg); inplug_status(); return;
  }
  if (!strncmp(spec, "/_inplug.on.", 12)) {
    char t[16]; snprintf(t, sizeof t, "%.15s", spec + 12);
    if (!ini_valid_title(t) || strlen(spec + 12) >= sizeof t) { look_kv("inplug/error", "use /_inplug.on.CUSA12345 or /_inplug.on.default"); return; }
    char hex[65]; long sz; if (plug_sha(hex, sizeof hex, &sz)) { look_kv("inplug/error", "plugin file not installed (or wrong size) - run /_inplug.get.<sha> first"); return; }
    if (ini_edit(t, msg, sizeof msg)) { look_kv("inplug/error", msg); return; }
    unlink(INP_OFF); inp_dir_open();
    sv_log("plugin round2_input ON for [%s] (%s)", t, msg); inp_pop("detector switched on for %s - (re)open the game", t);
    look_kv("inplug/result", "on - starts with the next launch of that game (close and reopen it if it is open)");
    look_kv("inplug/ini", msg); inplug_status(); return;
  }
  if (!strncmp(spec, "/_inplug.get.", 13)) {
    const char *want = spec + 13;
    if (strlen(want) != 64) { look_kv("inplug/error", "give the full 64-character SHA-256"); return; }
    for (int i = 0; i < 64; i++) { char c = want[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { look_kv("inplug/error", "SHA-256 must be lowercase hex"); return; } }
    char hdr[200], err[160]; unsigned char *d = 0; size_t len = 0;
    snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", look_key);
    char req[120]; int n = snprintf(req, sizeof req, "{\"id\":\"%s\",\"f\":\"round2_input\"}", console_id);
    int st = r2tls_fetch(ENGINE_HOST, "POST", "/ps4/plugin", hdr, req, n, &d, &len, PLUG_MAX + 1, err, sizeof err);
    if (st != 200 || !d) snprintf(msg, sizeof msg, "download failed (%d %s)", st, err);
    else if (len < PLUG_MIN || len > PLUG_MAX) snprintf(msg, sizeof msg, "download size %zu outside 1 KB-512 KB", len);
    else if (memcmp(d, "\x4f\x15\x3d\x1d", 4)) snprintf(msg, sizeof msg, "not a PS4 plugin file (header)");
    else {
      unsigned char h[32]; char hx[65]; mbedtls_sha256(d, len, h, 0); hexs(h, 32, hx);
      if (strcmp(hx, want)) snprintf(msg, sizeof msg, "file check failed: got %.12s, want %.12s", hx, want);
      else { mkdir("/data/GoldHEN", 0777); mkdir("/data/GoldHEN/plugins", 0777);
        if (write_atomic(GH_PLUG, (const char *)d, len)) snprintf(msg, sizeof msg, "couldn't save the plugin file");
        else if (chmod(GH_PLUG, 0755)) snprintf(msg, sizeof msg, "couldn't mark the plugin file runnable (errno %d)", errno); /* 1.1.67: rtld VOP_ACCESS needs exec */
        else { char chk[65]; long sz2; if (plug_sha(chk, sizeof chk, &sz2) || strcmp(chk, want)) snprintf(msg, sizeof msg, "saved file doesn't read back the same"); } }
    }
    if (d) free(d);
    if (msg[0]) { look_kv("inplug/error", msg); sv_log("plugin round2_input download: %s", msg); return; }
    sv_log("plugin round2_input installed (%zu bytes, sha %.12s)", len, want);
    look_kv("inplug/result", "installed (not switched on yet - use /_inplug.on.<game> )"); inplug_status(); return;
  }
  look_kv("inplug/error", "use /_inplug.status, /_inplug.get.<sha256>, /_inplug.on.<CUSA12345|default>, /_inplug.off, /_inplug.noaio");
}
static void countdown_tick(const char *key) {
  if (!cd_on || !key || !key[0]) return;
  long long now = (long long)time(0);
  /* 1.1.56: buttons pressed = the deadline moves to 2 min after the last press (pop-ups start over) */
  in_open(); int a = in_activity(0, 0);
  if (a != cd_input && (a < 0 || cd_input < 0)) { sv_log("countdown: controllers %s", a < 0 ? "NOT readable - timer only" : "readable - waiting for 2 min without play"); if (cd_input != -2 || a >= 0) inp_pop("%s", a < 0 ? "controller NOT readable - reset runs on its timer" : "controller readable - reset waits for 2 min without play"); }
  cd_input = a;
  if (a == 1) {
    static long long said; cd_last_in = now;
    if (cd_last_in + 120 > cd_deadline) {
      if (!cd_play_from && now >= cd_deadline - 120) cd_play_from = now;
      cd_deadline = cd_last_in + 120; cd_mask = 0; cd_ok = 0; cd_save();
      if (now - said >= 60) { said = now; sv_log("countdown: buttons pressed - reset moved to 2 min after the last press"); } /* 1.1.57: silent */
      { static long long ps; if (now - ps >= 20) { ps = now; inp_pop("controller in use - reset moved to 2 min after the last press"); } }
    }
  }
  long long play = cd_play_from ? now - cd_play_from : 0;
  { static int told; if (play >= 600 && !told) { told = 1; char st[400]; sv_log("countdown: still being played %lld min past the reset time - staff alerted", play / 60); report(key, st, sizeof st); write_status(st); } if (!play) told = 0; }
  if (play >= 3600) { cd_play_from = 0; cd_stop("still being played 60 min after the reset time - not reset (ask staff)", 0, 0); return; }
  long long left = cd_deadline - now;
  static const int mk[] = { 60, 30, 10 };
  static const char *msg[] = { "Round2: this PS4 resets in 1 minute - please save your game now.", "Round2: resetting in 30 seconds - save now.", "Round2: resetting in 10 seconds. Thanks for playing!" };
  for (int i = 0; i < 3; i++) if (left <= mk[i] && !(cd_mask & (1 << i))) { cd_mask |= 1 << i; cd_save(); if (left > mk[i] - 8 && file_exists(INP_POP)) notify(msg[i]); } /* 1.1.57: no pop-ups (owner 11:53pm: they break the guest experience) */
  if (left <= 20 && !cd_ok) {
    char why[200] = ""; int r = reset_confirm(key, why, sizeof why);
    if (r == 1) { cd_ok = 1; sv_log("countdown: engine confirmed the room is clear (live SimplyBook check)"); }
    else { cd_stop(why, r == 0, 0); return; }
  }
  if (left <= 0) {
    char lab[48]; snprintf(lab, sizeof lab, "%s", cd_lab); cd_on = 0; cd_ok = 0; cd_save();
    look_mute = 1; look_last[0] = 0; reset_go(lab); look_mute = 0;
    sv_log("countdown over: reset %s - %s", lab, look_last);
  }
}
static void look_reset(const char *spec) {
  char lab[48] = "", v[220]; int w = 0;
  if (!strcmp(spec, "/_reset.cancel")) {
    if (!cd_on) { look_kv("reset/result", "no countdown running"); return; }
    cd_stop("stopped by staff / extension", 1, 0); look_kv("reset/result", "countdown stopped"); return;
  }
  if (spec[7] == '.') { if (sscanf(spec, "/_reset.%40[A-Za-z0-9-]", lab) != 1) { look_kv("reset/error", "bad label"); return; }
    if (spec[7 + 1 + strlen(lab)] == '.') { if (sscanf(spec + 8 + strlen(lab), ".w%d", &w) != 1 || w < 0 || w > 900) { look_kv("reset/error", "bad wait"); return; } }
    else if (spec[8 + strlen(lab)]) { look_kv("reset/error", "bad spec"); return; } }
  else if (spec[7]) { look_kv("reset/error", "bad spec"); return; }
  if (cd_on) { look_kv("reset/refused", "a countdown is already running"); return; }
  if (w >= 15) {
    cd_on = 1; cd_ok = 0; cd_mask = 0; cd_deadline = (long long)time(0) + w; snprintf(cd_lab, sizeof cd_lab, "%s", lab[0] ? lab : "-");
    cd_last_in = 0; cd_play_from = 0; cd_input = -2; in_scan = 0; inp_prev_last = -1;
    if (w < 60) cd_mask |= 1; if (w < 30) cd_mask |= 2; cd_save();
    char m[200]; int mins = (w + 30) / 60;
    snprintf(m, sizeof m, "Round2: your session has ended. This PS4 resets in %d minute%s - please save your game now. Need more time? Ask our staff.", mins ? mins : 1, mins == 1 ? "" : "s");
    (void)m; sv_log("countdown started (silent): reset %s in %d s, then only after 2 min without buttons", cd_lab, w); /* 1.1.57: no pop-up */
    snprintf(v, sizeof v, "countdown %d s - pop-ups at the start, 60, 30, 10 s", w); look_kv("reset/countdown", v); return;
  }
  reset_go(lab);
}
static void clean_step(const char *key, int budget) {
  char st[16] = ""; int done = 0, total = 0, fails = 0, parts = 0;
  if (!clean_status(st, &done, &total, &fails, &parts) || (strcmp(st, "running") && strcmp(st, "paused"))) { clean_on = 0; return; }
  if (sv_game_open()) { if (strcmp(st, "paused")) { cl_wr("state", "paused\n"); sv_log("clean slate paused: a game is open"); } return; }
  if (strcmp(st, "running")) { cl_wr("state", "running\n"); sv_log("clean slate resumed"); }
  char pb[24]; cl_rd("pos", pb, sizeof pb); int pos = atoi(pb);
  FILE *td = fopen(CLND "/todo", "r"); if (!td) { cl_wr("state", "stopped\n"); clean_on = 0; return; }
  char line[100]; for (int i = 0; i < pos && fgets(line, sizeof line, td); i++) {}
  long long t0 = (long long)time(0);
  while (pos < total && (long long)time(0) - t0 < budget && fgets(line, sizeof line, td)) {
    char u[16] = "", t[16] = ""; if (sscanf(line, "%15s %15s", u, t) != 2) { pos++; continue; }
    if (sv_game_open()) { cl_wr("state", "paused\n"); sv_log("clean slate paused: a game is open"); break; }
    sceSystemServicePowerTick(); sceShellCoreUtilResetAutoPowerDownTimer();
    { char rm[48] = ""; cl_rd("mode", rm, sizeof rm);
      if (!strncmp(rm, "memload ", 8)) { /* 1.1.84: member load, game by game */
        double d0 = g_t_dl, w0 = g_t_wr, k0 = g_t_mk, g0 = tm_now(); char why[200] = "", tb[48] = "";
        char gn[64], pn[40]; sw_game_name(t, gn, sizeof gn); pname(u, pn, sizeof pn);
        bpop_key("clprog", "Cloud -> PS4: downloading %s for %s (%d of %d)...", gn, pn, done + 1, total);
        int r = memload_game(key, u, t, why, sizeof why); double gt = tm_now() - g0;
        if (r) bpop_key("clprog", "%s could not be loaded for %s - staff will check", gn, pn); else bpop_key("clprog", "PS4 -> profile: %s is in %s's profile (%d of %d)", gn, pn, done + 1, total);
        double tot = 0; cl_rd("rtime", tb, sizeof tb); tot = atof(tb) + gt; cl_wr("rtime", "%.1f\n", tot);
        if (r) { fails++; cl_err("%s game %s not loaded: %s", u, t, why); }
        done++; pos++;
        sv_log("restore test: user %s game %s %s in %.1f s (download %.1f s, into profile %.1f s, making parts %.1f s)%s%s", u, t, r ? "FAILED" : "ok", gt, g_t_dl - d0, g_t_wr - w0, g_t_mk - k0, r ? " - " : "", r ? why : "");
        cl_wr("pos", "%d\n", pos); cl_wr("stat", "%d %d %d %d\n", done, total, fails, parts);
        continue;
      }
      if (!strncmp(rm, "restore ", 8)) { /* 1.1.77: "/_restoreall" - each game moved back in from the restore point, timed */
        char stp[24] = "", su[16] = "", ms[300], tb[48] = ""; sscanf(rm + 8, "%23s %15s", stp, su); /* 1.1.82: su = source profile (member prep) */
        snprintf(ms, sizeof ms, "/_gmove.%s.%s.%s/%s/%s/_game/%s", u, t, console_id, su[0] ? su : u, t, stp);
        double d0 = g_t_dl, w0 = g_t_wr, b0 = g_t_bk, k0 = g_t_mk, g0 = tm_now(); unsigned long long by0 = g_b_dl;
        look_mute = 1; look_last[0] = 0; int r = look_gmove(key, ms); look_mute = 0;
        if (r) { sv_log("restore test: user %s game %s NOT restored - %s", u, t, look_last[0] ? look_last : "see save.log"); cl_err("%s game %s not restored: %s", u, t, look_last[0] ? look_last : "?"); }
        double gt = tm_now() - g0;
        double tot = 0; cl_rd("rtime", tb, sizeof tb); tot = atof(tb) + gt; cl_wr("rtime", "%.1f\n", tot);
        if (r) fails++;
        done++; pos++;
        sv_log("restore test: user %s game %s %s in %.1f s (download %.1f s %llu KB, into profile %.1f s, making parts %.1f s, backing up the profile first %.1f s)",
               u, t, r ? "FAILED" : "ok", gt, g_t_dl - d0, (g_b_dl - by0) / 1024, g_t_wr - w0, g_t_mk - k0, g_t_bk - b0);
        bpop_key("clprog", "Download test: %d of %d games, %d failed. Last: %s %.1f s (download %.1f s, into profile %.1f s). Total so far %.0f s",
                 done, total, fails, t, gt, g_t_dl - d0, g_t_wr - w0, tot);
        FILE *ix = fopen(CLND "/index", "a"); if (ix) { fprintf(ix, "%s %s restore %s %.1f s dl %.1f wr %.1f mk %.1f bk %.1f\n", u, t, r ? "FAILED" : "ok", gt, g_t_dl - d0, g_t_wr - w0, g_t_mk - k0, g_t_bk - b0); fclose(ix); }
        cl_wr("pos", "%d\n", pos); cl_wr("stat", "%d %d %d %d\n", done, total, fails, parts);
        continue;
      } }
    /* 1.1.51: INTERNAL FIRST - every part copied to /data/round2/prep with fingerprints + queued for the cloud batch;
       only if every part of the game copied cleanly are the parts removed. No upload here (owner 7:43pm). */
    char base[200] = "internal"; static char nm[64][32]; int n = sv_disk_parts(u, t, nm, 64), br = 0, nf = 0, cov = 0; unsigned long long nb = 0;
    static char copied[64]; memset(copied, 0, sizeof copied);
    char md[40] = ""; long long pst = 0; cl_rd("mode", md, sizeof md); int purge = !strncmp(md, "purge ", 6); if (purge) pst = atoll(md + 6);
    { char gn[64], pn[40]; sw_game_name(t, gn, sizeof gn); pname(u, pn, sizeof pn); bpop_key("clprog", "Profile -> PS4: copying %s's %s save to this PS4 (%d of %d)...", pn, gn, done + 1, total); }
    for (int i = 0; i < n && !br; i++) { char why[160] = ""; int f1 = 0; unsigned long long b1 = 0;
      if (purge && snap_covered(u, t, nm[i], pst)) { cov++; continue; } /* 1.1.52: already safe in the restore point, unchanged since */
      if (sv_local_copy(u, t, nm[i], &f1, &b1, why, sizeof why)) { br = -1; sv_log("clean slate: internal copy FAILED %s %s %s: %s", u, t, nm[i], why); cl_err("%s game %s: internal copy failed (%s) - game LEFT on the PS4", u, t, why); } else { nf += f1; nb += b1; copied[i] = 1; } }
    if (!br && n - cov > 0) { char cb[48]; long long cp0 = 0, cb0 = 0; cl_rd("copied", cb, sizeof cb); sscanf(cb, "%lld %lld", &cp0, &cb0); cl_wr("copied", "%lld %lld\n", cp0 + (n - cov), cb0 + (long long)nb); }
    if (!br) sv_log("clean slate: user %s game %s: %d part(s): %d copied internally (%d files, %llu bytes), %d already in the restore point", u, t, n, n - cov, nf, nb, cov);
    if (cov) snprintf(base, sizeof base, "internal+restore-point(%d)", cov);
    int gone = 0, left = 0;
    int copyonly = !strcmp(md, "copy"); /* 1.1.74: "/_copy" = internal copy ONLY - nothing is ever removed */
    if (br == 0 && !copyonly) {
      for (int i = 0; i < n; i++) { char res[300]; if (sv_del_part(u, t, nm[i], res, sizeof res)) left++; else gone++;
        if (copied[i]) { char mf[300]; snprintf(mf, sizeof mf, "/data/round2/prep/%s/%s/%s/manifest.txt", u, t, nm[i]); if (!file_exists(mf)) { sv_log("clean slate: WARNING internal copy of %s %s %s is MISSING after removal", u, t, nm[i]); cl_err("%s game %s part %s: internal copy missing after removal", u, t, nm[i]); } } }
      if (left) cl_err("%s game %s: %d part(s) could not be removed (still on the PS4)", u, t, left);
      char d[200]; snprintf(d, sizeof d, "/user/home/%s/savedata/%s", u, t); rmdir(d);
      snprintf(d, sizeof d, "/user/home/%s/savedata_meta/user/%s", u, t); rmdir(d);
    }
    if (br != 0 || left) fails++;
    parts += gone; done++; pos++;
    FILE *ix = fopen(CLND "/index", "a"); if (ix) { fprintf(ix, "%s %s backup %s removed %d left %d %s\n", u, t, br ? "FAILED" : "ok", gone, left, base); fclose(ix); }
    if (copyonly) sv_log("copy only: user %s game %s: internal copy %s (nothing removed)", u, t, br ? "FAILED" : "ok");
    else sv_log("clean slate: user %s game %s: internal copy %s, %d part(s) removed, %d left", u, t, br ? "FAILED - game left untouched" : "ok", gone, left);
    cl_wr("pos", "%d\n", pos); cl_wr("stat", "%d %d %d %d\n", done, total, fails, parts);
    { static long long lr; char rk2[16] = ""; cl_rd("run", rk2, sizeof rk2); if (!strncmp(rk2, "prep", 4) && time(0) - lr >= 4) { char s3[400]; lr = time(0); report(key, s3, sizeof s3); } } /* 1.1.101: the tile's bar follows each game */
    { char gn[64], pn[40]; sw_game_name(t, gn, sizeof gn); pname(u, pn, sizeof pn);
      if (br) bpop_key("clprog", "%s: %s's save could NOT be copied - left on the profile (%d of %d)", gn, pn, done, total);
      else if (copyonly) bpop_key("clprog", "%s: %s's save is safe on this PS4 (%d of %d)", gn, pn, done, total);
      else bpop_key("clprog", "%s: saved to this PS4, then cleared from %s's profile (%d of %d)", gn, pn, done, total); } /* 1.1.102 plain words */
  }
  fclose(td);
  if (pos >= total) { cl_wr("state", "done\n"); clean_on = 0; sv_log("clean slate DONE: %d games, %d failed, %d parts removed", done, fails, parts); clean_finish(done, fails, parts);
    { char rm[48] = "", tb[48] = ""; cl_rd("mode", rm, sizeof rm); if (!strncmp(rm, "restore ", 8) || !strncmp(rm, "memload ", 8)) { cl_rd("rtime", tb, sizeof tb);
      char rn[120] = "", kd[16] = "", lb[48] = ""; long long t0 = 0; cl_rd("run", rn, sizeof rn); sscanf(rn, "%15s %lld %47s", kd, &t0, lb);
      if (!strcmp(kd, "prep")) { for (char *c = lb; *c; c++) if (*c == '_') *c = ' ';
        sv_log("member prep DONE: %s - %d games, %d failed, %.0f s", lb, done, fails, atof(tb)); bpop_key("clprog", "%s is READY: %d game%s loaded%s (%.0f s)", lb, done - fails, done - fails == 1 ? "" : "s", fails ? " - some failed, staff will check" : "", atof(tb)); }
      if (!strcmp(kd, "prep") && file_exists(MEMQ)) memplay_next(key); /* 1.1.94: next ticked player */
      if (!strcmp(kd, "prep") && !file_exists(MEMQ)) { unlink("/data/round2/memqn"); keep_order(1); g_after_load = time(0); } /* 1.1.101: batch finished – profiles back in order; 1.1.119: pop-up once the tile is gone */
      if (!strcmp(kd, "prep")) { char s4[400]; report(key, s4, sizeof s4); } /* tell the tile at once (next player started, or all ready) */
      sv_log("restore test DONE: %d games, %d failed, %.0f s in all (%.1f s a game on average)", done, fails, atof(tb), done ? atof(tb) / done : 0.0);
      if (strcmp(kd, "prep")) bpop_key("clprog", "Download test DONE: %d games, %d failed, %.0f s in all (%.1f s a game)", done, fails, atof(tb), done ? atof(tb) / done : 0.0); } }
    { char rn2[120] = "", kd2[16] = ""; cl_rd("run", rn2, sizeof rn2); sscanf(rn2, "%15s", kd2); if (!strcmp(kd2, "switch")) switch_resume(key); } }   /* 1.1.124 */
  if ((long long)time(0) - snap_hb >= 300 || !clean_on) { char s2[400]; snap_hb = (long long)time(0); report(key, s2, sizeof s2); write_status(s2); }
}
/* ---- 1.1.52 LAUNCH PURGE (owner 17 Nov 8:05pm: "clear all saves IF restore point succeeded"). "/_purge.<label>" = clean slate of
   EVERY profile on this PS4 (main included; never PS Vue, the protected main-profile game, homebrew settings), but only if the
   snapshot <label> (the day0 restore point) finished with 0 failures. A part that is in that restore point and hasn't changed
   since is removed without another copy; anything newer or missing from it is copied internally first (INTERNAL FIRST). Before
   it starts, the old internal folder is kept aside (renamed prep-before-<label>-<time>, nothing deleted) and a fresh empty one
   made, so tomorrow's tests start from nothing. Runs as the clean slate job (report "clean", /_clean.stop). */
static int snap_covered(const char *u, const char *t, const char *nm, long long stamp) {
  char img[240]; struct stat st; snprintf(img, sizeof img, "/user/home/%s/savedata/%s/sdimg_%s", u, t, nm);
  if (stat(img, &st) || (long long)st.st_mtime > stamp) return 0;
  FILE *ix = fopen(SNAPD "/index", "r"); if (!ix) return 0;
  char ln[200], a[16], b[16], c[48]; int f, fl, hit = 0; unsigned long long by;
  while (!hit && fgets(ln, sizeof ln, ix)) if (sscanf(ln, "%15s %15s %47s %d %llu %d", a, b, c, &f, &by, &fl) == 6 && !fl && f > 0 && !strcmp(a, u) && !strcmp(b, t) && !strcmp(c, nm)) hit = 1;
  fclose(ix); return hit;
}
static void look_purge(const char *spec) {
  char lab[24] = "", slab[24] = "", stamp[24] = "", sst[16] = "", st[16] = "", v[200]; int dn = 0, tt = 0, fl = 0; long long by = 0;
  if (sscanf(spec, "/_purge.%16[a-z0-9-]", lab) != 1) { look_kv("purge/error", "bad label"); return; }
  if (!snap_status(slab, stamp, sst, &dn, &tt, &fl, &by) || strcmp(slab, lab)) { look_kv("purge/refused", "no such restore point on this PS4 - nothing cleared"); return; }
  if (strcmp(sst, "done") || fl || dn < tt) { snprintf(v, sizeof v, "restore point %s not complete (%s, %d/%d, %d failed) - nothing cleared", lab, sst, dn, tt, fl); look_kv("purge/refused", v); return; }
  cl_rd("state", st, sizeof st); if (!strcmp(st, "running") || !strcmp(st, "paused")) { look_kv("purge/refused", "a clean slate is already running"); return; }
  if (sv_game_open()) { look_kv("purge/refused", "a game is open - close it first (/_close)"); return; }
  RegUserIdList L; memset(&L, 0xff, sizeof L); sceUserServiceGetRegisteredUserIdList(&L);
  uint32_t mainu = r2_main_user();
  for (int i = 0; i < 16; i++) { if (L.userId[i] == -1 || !L.userId[i]) continue; if ((uint32_t)L.userId[i] != mainu && user_signed_in((uint32_t)L.userId[i])) { look_kv("purge/refused", "a spare profile is signed in - sign it out first"); return; } }
  /* fresh internal folder; the old one is kept aside */
  struct stat ps; if (!stat("/data/round2/prep", &ps)) { char old[120]; snprintf(old, sizeof old, "/data/round2/prep-before-%s-%lld", lab, (long long)time(0));
    if (rename("/data/round2/prep", old)) { snprintf(v, sizeof v, "can't set the old internal folder aside (errno %d) - nothing cleared", errno); look_kv("purge/error", v); return; } sv_log("purge: old internal folder kept as %s", old); }
  mkdir("/data/round2/prep", 0777); put_file("/data/round2/prep/pending.txt", "");
  mkdir(CLND, 0777);
  FILE *td = fopen(CLND "/todo", "w"); if (!td) { look_kv("purge/error", "can't write the list"); return; }
  int ng = 0, nu = 0, skip = 0; static char nm[64][32];
  for (int i = 0; i < 16; i++) {
    if (L.userId[i] == -1 || !L.userId[i]) continue;
    char u[9]; snprintf(u, sizeof u, "%08x", (uint32_t)L.userId[i]); nu++;
    char sd[120]; snprintf(sd, sizeof sd, "/user/home/%s/savedata", u); DIR *g = opendir(sd); struct dirent *f;
    while (g && (f = readdir(g))) {
      if (strlen(f->d_name) != 9) continue;
      if (!strcmp(f->d_name, VUE_ID) || r2_homebrew(f->d_name) || (r2_protected(f->d_name) && (uint32_t)L.userId[i] == mainu)) { skip++; continue; }
      if (!sv_disk_parts(u, f->d_name, nm, 64)) continue;
      fprintf(td, "%s %s\n", u, f->d_name); ng++;
    }
    if (g) closedir(g);
  }
  fclose(td);
  cl_begin("purge", lab);
  cl_wr("pos", "0\n"); cl_wr("stat", "0 %d 0 0\n", ng); cl_wr("index", ""); cl_wr("mode", "purge %s\n", stamp); cl_wr("state", ng ? "running\n" : "done\n");
  clean_on = ng > 0;
  sv_log("purge started (restore point %s, stamp %s, %d parts in it): %d profile(s), %d game(s), %d kept (PS Vue/protected/homebrew)", lab, stamp, dn, nu, ng, skip);
  snprintf(v, sizeof v, "%d games on %d profiles (%d kept)", ng, nu, skip); look_kv("purge/started", v);
}
/* ---- 1.1.52 BACKGROUND CLOUD BATCH (owner 17 Nov 7:43pm: internal first, "batch save them in the background"; 8:05pm: code it
   now, but no cloud backups needed yet - no membership yet). OFF unless /data/round2/prep/cloud_on exists ("/_cloud.on",
   "/_cloud.off"). When on, once a minute while no game is open and no snapshot/clean slate is running, it works ~20 s through
   prep/pending.txt: for each part, if its manifest.txt is identical to the one last sent (uploaded.txt) the cloud already has
   it - skipped, no upload. Otherwise every file is read from the internal copy, its SHA-256 checked against the manifest (a
   damaged copy is never sent), sent in 1 MB pieces to <console>/<user>/<game>/<part>/<time>/ with the usual manifest (so
   /_srest can restore it), and uploaded.txt + cloud.txt are written. A failing part is retried next minute, up to 5 times,
   then listed in cloud_failed.txt. Costs Worker requests only (no Durable Object calls). Report: "cloud". */
#define PREPD "/data/round2/prep"
static int pr_rd(const char *f, char *b, size_t n) { char p[100]; snprintf(p, sizeof p, PREPD "/%s", f); FILE *x = fopen(p, "r"); if (!x) { b[0] = 0; return -1; }
  size_t r = fread(b, 1, n - 1, x); fclose(x); b[r] = 0; while (r && (b[r - 1] == '\n' || b[r - 1] == ' ')) b[--r] = 0; return 0; }
static void pr_wr(const char *f, const char *fmt, ...) { char p[100]; snprintf(p, sizeof p, PREPD "/%s", f); FILE *x = fopen(p, "w"); if (!x) return;
  va_list ap; va_start(ap, fmt); vfprintf(x, fmt, ap); va_end(ap); fflush(x); fsync(fileno(x)); fclose(x); }
static char *read_all(const char *p, size_t *n, size_t max) {
  FILE *f = fopen(p, "r"); if (!f) return 0; char *b = malloc(max + 1); if (!b) { fclose(f); return 0; }
  *n = fread(b, 1, max, f); b[*n] = 0; fclose(f); return b;
}
static int cloud_waiting(void) {
  char a[32]; pr_rd("cloud_pos", a, sizeof a); long pos = atol(a);
  FILE *q = fopen(PREPD "/pending.txt", "r"); if (!q) return 0;
  if (fseek(q, pos, SEEK_SET)) { fclose(q); return 0; }
  int n = 0, c; while ((c = fgetc(q)) != EOF) if (c == '\n') n++;
  fclose(q); return n;
}
static void cloud_report(void) {
  struct stat st; if (stat(PREPD, &st)) return;
  char a[80]; int up = 0, same = 0, fl = 0; long long by = 0; pr_rd("cloud_stat", a, sizeof a); sscanf(a, "%d %d %d %lld", &up, &same, &fl, &by);
  jadd(",\"cloud\":{\"on\":%d,\"waiting\":%d,\"up\":%d,\"same\":%d,\"fails\":%d,\"mb\":%lld}", file_exists(PREPD "/cloud_on"), cloud_waiting(), up, same, fl, by / 1048576);
}
/* 0 = in the cloud (sent now, or *same = already there); -1 = try again later; -2 = nothing to send (no internal copy) */
/* 1.1.84: the member's index (members/<id>/<slot>/index.txt = "<game> <part> <cloud folder> [<data fingerprint>]" per line,
   newest wins). 1.1.86: kept in memory for 10 min (one fetch per batch, not per part). */
static char mi_tag[64]; static unsigned char *mi_txt; static size_t mi_len; static long long mi_at;
static const unsigned char *mi_get(const char *key, const char *mtag, size_t *len) {
  if (mi_txt && !strcmp(mi_tag, mtag) && (long long)time(0) - mi_at < 600) { *len = mi_len; return mi_txt; }
  free(mi_txt); mi_txt = 0; mi_len = 0;
  char k[200]; snprintf(k, sizeof k, "members/%s/index.txt", mtag); unsigned char *d = 0; size_t l = 0;
  if (sv_get_raw(key, k, &d, &l, 0, 0)) { free(d); d = malloc(1); l = 0; } /* missing = new member */
  mi_txt = d; mi_len = l; snprintf(mi_tag, sizeof mi_tag, "%s", mtag); mi_at = (long long)time(0); *len = l; return mi_txt;
}
/* 1.1.86: fingerprint of a part's DATA (manifest lines outside sce_sys/ - those labels differ per profile) */
static void data_sha(const char *man, size_t mn, char *hex) {
  mbedtls_sha256_context h; mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h, 0);
  for (const char *ln = man, *e = man + mn; ln < e; ) { const char *nx = memchr(ln, '\n', (size_t)(e - ln)); size_t l = nx ? (size_t)(nx - ln) : (size_t)(e - ln);
    if (l && strncmp(ln, "sce_sys/", 8)) { mbedtls_sha256_update(&h, (const unsigned char *)ln, l); mbedtls_sha256_update(&h, (const unsigned char *)"\n", 1); }
    ln += l + 1; }
  unsigned char hs[32]; mbedtls_sha256_finish(&h, hs); mbedtls_sha256_free(&h); hexs(hs, 32, hex);
}
static int mem_index_put(const char *key, const char *mtag, const char *t, const char *nm, const char *base, const char *dsha) {
  char k[200]; snprintf(k, sizeof k, "members/%s/index.txt", mtag);
  size_t ol = 0; const unsigned char *old0 = mi_get(key, mtag, &ol); unsigned char *old = 0; if (ol) { old = malloc(ol); if (old) memcpy(old, old0, ol); }
  size_t cap = ol + 600; char *nw = malloc(cap); if (!nw) { free(old); return -1; } size_t n = 0;
  if (old) { char *b = malloc(ol + 1); if (b) { memcpy(b, old, ol); b[ol] = 0;
      for (char *ln = b, *nx; ln && *ln; ln = nx) { nx = strchr(ln, '\n'); if (nx) *nx++ = 0; char a1[16] = "", a2[64] = ""; if (sscanf(ln, "%15s %63s", a1, a2) == 2 && !strcmp(a1, t) && !strcmp(a2, nm)) continue; if (*ln) n += (size_t)snprintf(nw + n, cap - n, "%s\n", ln); }
      free(b); } free(old); }
  n += (size_t)snprintf(nw + n, cap - n, "%s %s %s %s\n", t, nm, base, dsha ? dsha : "-");
  int r = sv_put(key, k, (const unsigned char *)nw, n);
  if (!r) { free(mi_txt); mi_txt = (unsigned char *)nw; mi_len = n; snprintf(mi_tag, sizeof mi_tag, "%s", mtag); mi_at = (long long)time(0); } else free(nw);
  return r;
}
static int cloud_part_m(const char *key, const char *u, const char *t, const char *nm, const char *mtag, int *same, unsigned long long *sent, char *why, size_t wn);
static int cloud_part(const char *key, const char *u, const char *t, const char *nm, int *same, unsigned long long *sent, char *why, size_t wn) { return cloud_part_m(key, u, t, nm, "", same, sent, why, wn); }
static int cloud_part_m(const char *key, const char *u, const char *t, const char *nm, const char *mtag, int *same, unsigned long long *sent, char *why, size_t wn) {
  char d[220], p[320]; size_t mn = 0, un = 0; *same = 0; *sent = 0;
  snprintf(d, sizeof d, PREPD "/%s/%s/%s", u, t, nm);
  snprintf(p, sizeof p, "%s/manifest.txt", d); char *man = read_all(p, &mn, 256 * 1024);
  if (!man || !mn) { free(man); snprintf(why, wn, "no internal copy"); return -2; }
  snprintf(p, sizeof p, "%s/uploaded.txt", d); char *upl = read_all(p, &un, 256 * 1024);
  { char cp[320], cb[220] = ""; snprintf(cp, sizeof cp, "%s/cloud.txt", d); FILE *cf = fopen(cp, "r"); if (cf) { if (!fgets(cb, sizeof cb, cf)) cb[0] = 0; fclose(cf); }
    char want[120]; if (mtag[0]) snprintf(want, sizeof want, "members/%s/", mtag); else snprintf(want, sizeof want, "%s/", console_id);
    int same_dest = !strncmp(cb, want, strlen(want)); /* 1.1.84: unchanged AND already in the same place (same member) */
    if (upl && un == mn && !memcmp(upl, man, mn) && same_dest) {
      if (mtag[0]) { /* 1.1.86: make sure the member's index carries this part's data fingerprint (older entries don't) */
        char ds[65]; data_sha(man, mn, ds); size_t il = 0; const unsigned char *ix = mi_get(key, mtag, &il); int has = 0;
        char *b = malloc(il + 1); if (b) { memcpy(b, ix, il); b[il] = 0; for (char *ln = b, *nx; ln && *ln && !has; ln = nx) { nx = strchr(ln, '\n'); if (nx) *nx++ = 0; char a1[16], a2[64], a3[220], a4[70]; if (sscanf(ln, "%15s %63s %219s %69s", a1, a2, a3, a4) == 4 && !strcmp(a1, t) && !strcmp(a2, nm) && !strcmp(a4, ds)) has = 1; } free(b); }
        cb[strcspn(cb, "\r\n")] = 0; if (!has && cb[0]) mem_index_put(key, mtag, t, nm, cb, ds); }
      free(man); free(upl); *same = 1; return 0; }
    struct stat mist; if (mtag[0] && stat("/data/round2/mi-skip-off", &mist)) { /* 1.1.86: the member already has exactly this data (loaded into another profile) -> nothing to send */
      char ds[65]; data_sha(man, mn, ds); size_t il = 0; const unsigned char *ix = mi_get(key, mtag, &il);
      char *b = malloc(il + 1); if (b) { memcpy(b, ix, il); b[il] = 0; int hit = 0;
        for (char *ln = b, *nx; ln && *ln && !hit; ln = nx) { nx = strchr(ln, '\n'); if (nx) *nx++ = 0; char a1[16], a2[64], a3[220], a4[70]; if (sscanf(ln, "%15s %63s %219s %69s", a1, a2, a3, a4) == 4 && !strcmp(a1, t) && !strcmp(a2, nm) && !strcmp(a4, ds)) hit = 1; }
        free(b);
        if (hit) { snprintf(p, sizeof p, "%s/uploaded.txt", d); FILE *uf = fopen(p, "w"); if (uf) { fwrite(man, 1, mn, uf); fclose(uf); } free(man); free(upl); *same = 1; sv_log("cloud batch: %s %s %s already in member %s's folder - not sent", u, t, nm, mtag); return 0; } } } }
  free(upl);
  if (!sv_buf) { void *b = mmap(0, SV_PIECE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (b == MAP_FAILED) { free(man); snprintf(why, wn, "no memory"); return -1; } sv_buf = b; }
  char base[220]; if (mtag[0]) snprintf(base, sizeof base, "members/%s/%s/%s/%lld", mtag, t, nm, (long long)time(0)); else snprintf(base, sizeof base, "%s/%s/%s/%s/%lld", console_id, u, t, nm, (long long)time(0));
  sv_man_n = 0; if (sv_man) sv_man[0] = 0;
  int bad = 0, nfiles = 0;
  for (char *ln = man, *nx; ln && *ln && !bad; ln = nx) {
    nx = strchr(ln, '\n'); if (nx) *nx++ = 0;
    char *t1 = strchr(ln, '\t'); if (!t1) continue; *t1++ = 0; char *t2 = strchr(t1, '\t'); if (!t2) continue; *t2++ = 0;
    const char *r = ln; unsigned long long sz = strtoull(t1, 0, 10); const char *hx = t2;
    if (strstr(r, "..")) { bad = 1; snprintf(why, wn, "bad name in manifest"); break; }
    char fp[600]; snprintf(fp, sizeof fp, "%s/%s", d, r);
    int fd = open(fp, O_RDONLY); if (fd < 0) { bad = 1; snprintf(why, wn, "internal file missing: %.80s", r); break; }
    mbedtls_sha256_context h; mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h, 0);
    unsigned long long done = 0; int pieces = 0;
    do {
      size_t want = sz - done > SV_PIECE ? SV_PIECE : (size_t)(sz - done), got = 0;
      while (got < want) { ssize_t x = read(fd, sv_buf + got, want - got); if (x <= 0) break; got += (size_t)x; }
      if (got != want) { bad = 1; snprintf(why, wn, "internal file short: %.80s", r); break; }
      mbedtls_sha256_update(&h, sv_buf, got);
      char k[760]; snprintf(k, sizeof k, "%s/f/%s~%d", base, r, pieces);
      if (sv_put(key, k, sv_buf, got)) { bad = 1; snprintf(why, wn, "upload failed: %.80s", r); break; }
      snap_tick(key);
      done += got; pieces++;
    } while (done < sz);
    close(fd);
    unsigned char hs[32]; char hh[65]; mbedtls_sha256_finish(&h, hs); mbedtls_sha256_free(&h); hexs(hs, 32, hh);
    if (!bad && strncmp(hh, hx, 64)) { bad = 1; snprintf(why, wn, "fingerprint differs - internal copy damaged: %.80s", r); sv_log("cloud batch: DAMAGED internal copy %s %s %s file %s", u, t, nm, r); }
    if (bad) break;
    char line[620]; snprintf(line, sizeof line, "%s\t%llu\t%d\n", r, sz, pieces); sv_man_add(line); nfiles++; *sent += sz;
  }
  free(man);
  if (!bad) { char mk[260]; snprintf(mk, sizeof mk, "%s/manifest.txt", base); if (sv_put(key, mk, (const unsigned char *)(sv_man ? sv_man : ""), sv_man_n)) { bad = 1; snprintf(why, wn, "manifest upload failed"); } }
  if (bad) return -1;
  /* remember what is up: the manifest as it was when read (re-read: same thread, nothing changed it meanwhile) */
  snprintf(p, sizeof p, "%s/manifest.txt", d); size_t n2 = 0; char *m2 = read_all(p, &n2, 256 * 1024);
  snprintf(p, sizeof p, "%s/uploaded.txt", d); FILE *f = fopen(p, "w"); if (f && m2) fwrite(m2, 1, n2, f); if (f) { fflush(f); fsync(fileno(f)); fclose(f); } free(m2);
  snprintf(p, sizeof p, "%s/cloud.txt", d); put_file(p, base);
  if (mtag[0]) { char ds[65]; size_t m3n = 0; char mp3[320]; snprintf(mp3, sizeof mp3, "%s/manifest.txt", d); char *m3 = read_all(mp3, &m3n, 256 * 1024); if (m3) data_sha(m3, m3n, ds); else snprintf(ds, sizeof ds, "-"); free(m3);
    if (mem_index_put(key, mtag, t, nm, base, ds)) { snprintf(why, wn, "member index not updated"); return -1; } }
  sv_log("cloud batch: sent %s %s %s: %d files, %llu bytes -> %s", u, t, nm, nfiles, *sent, base);
  { char gn[64], pn[40]; sw_game_name(t, gn, sizeof gn); pname(u, pn, sizeof pn); bpop_key("cprog", "PS4 -> Round2 cloud: %s (%s) uploaded%s", gn, pn, strncmp(base, "members/", 8) ? "" : " to the crew's account"); }
  return 0;
}
static void cloud_step(const char *key, int budget) {
  if (!file_exists(PREPD "/cloud_on") || sv_game_open()) return;
  char a[80]; pr_rd("cloud_pos", a, sizeof a); long pos = atol(a);
  int up = 0, same = 0, fl = 0, tries = 0; long long by = 0; pr_rd("cloud_stat", a, sizeof a); sscanf(a, "%d %d %d %lld", &up, &same, &fl, &by);
  pr_rd("cloud_tries", a, sizeof a); tries = atoi(a);
  FILE *q = fopen(PREPD "/pending.txt", "r"); if (!q) return;
  struct stat qs; fstat(fileno(q), &qs);
  if (pos > (long)qs.st_size) pos = 0;
  fseek(q, pos, SEEK_SET);
  long long t0 = (long long)time(0); char ln[200]; int worked = 0;
  while ((long long)time(0) - t0 < budget && fgets(ln, sizeof ln, q)) {
    if (sv_game_open()) break;
    char u[16] = "", t[16] = "", nm[48] = ""; long next = ftell(q);
    if (sscanf(ln, "%15s %15s %47s", u, t, nm) != 3 || strchr(nm, '/') || strstr(nm, "..")) { pos = next; continue; }
    int sm = 0; unsigned long long sent = 0; char why[160] = "";
    char mtag[64] = ""; { const char *mp = strstr(ln, " m="); if (mp) { sscanf(mp + 3, "%63s", mtag); if (strstr(mtag, "..")) mtag[0] = 0; } }
    int r = cloud_part_m(key, u, t, nm, mtag, &sm, &sent, why, sizeof why); worked = 1;
    if (r == 0) { if (sm) same++; else { up++; by += (long long)sent; } tries = 0; pos = next; }
    else if (r == -2 || ++tries >= 5) {
      fl++; tries = 0; pos = next;
      FILE *ff = fopen(PREPD "/cloud_failed.txt", "a"); if (ff) { fprintf(ff, "%s %s %s %lld %s\n", u, t, nm, (long long)time(0), why); fclose(ff); }
      sv_log("cloud batch: GAVE UP on %s %s %s: %s", u, t, nm, why);
    } else { sv_log("cloud batch: %s %s %s not sent (try %d of 5): %s", u, t, nm, tries, why); pr_wr("cloud_tries", "%d\n", tries); break; }
    pr_wr("cloud_pos", "%ld\n", pos); pr_wr("cloud_stat", "%d %d %d %lld\n", up, same, fl, by); pr_wr("cloud_tries", "%d\n", tries);
    bpop_key("cprog", "Cloud copy: %d sent (%lld MB), %d unchanged, %d failed - %d still waiting", up, by / 1048576, same, fl, cloud_waiting()); /* 1.1.76 */
  }
  int atend = pos >= (long)qs.st_size && feof(q);
  fclose(q);
  if (atend && worked) { /* all sent: start the list afresh (same thread as the copies, so nothing is appended meanwhile) */
    struct stat q2; if (!stat(PREPD "/pending.txt", &q2) && (long)q2.st_size == pos) { put_file(PREPD "/pending.txt", ""); pr_wr("cloud_pos", "0\n"); sv_log("cloud batch: list done (%d sent, %d unchanged, %d failed so far)", up, same, fl); } }
}
static void look_cloud(const char *spec) {
  char v[160];
  if (!strcmp(spec, "/_cloud.on")) { mkdir(PREPD, 0777); put_file(PREPD "/cloud_on", "1"); sv_log("cloud batch switched ON"); }
  else if (!strcmp(spec, "/_cloud.off")) { unlink(PREPD "/cloud_on"); sv_log("cloud batch switched OFF"); }
  else if (strcmp(spec, "/_cloud.state")) { look_kv("cloud/error", "use /_cloud.on, /_cloud.off or /_cloud.state"); return; }
  char a[80]; int up = 0, same = 0, fl = 0; long long by = 0; pr_rd("cloud_stat", a, sizeof a); sscanf(a, "%d %d %d %lld", &up, &same, &fl, &by);
  snprintf(v, sizeof v, "%s, %d waiting, %d sent, %d unchanged, %d failed", file_exists(PREPD "/cloud_on") ? "ON" : "OFF", cloud_waiting(), up, same, fl);
  look_kv("cloud/result", v);
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
  bpop("Going to sleep now (asked by the control centre)");
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
  /* 1.1.74: kept in a file so a helper update/restart doesn't drop it (Kaeden 19 Nov: slept after each update) */
  if (g_awake_until) { char b[24]; snprintf(b, sizeof b, "%lld", g_awake_until); put_file("/data/round2/keepawake.txt", b); } else unlink("/data/round2/keepawake.txt");
  if (mins) { sceSystemServicePowerTick(); sceShellCoreUtilResetAutoPowerDownTimer(); }
  if (mins) bpop("Staying awake for %d min (asked by the control centre)", mins); else bpop("Stay-awake cleared - normal power saving");
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
#define OWNPID "/data/round2/own-pid.txt"
/* 1.1.71 (slimming): the 16 Nov own-program TEST payload is no longer carried (74 KB) – own-program mode is proven and used. */
static void look_own(void) { look_kv("own/result", "removed in 1.1.71 (the test payload is no longer built in)"); }
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
  if (own_spawns() >= OWN_SPAWN_MAX) { look_kv("selfown/result", "refused: too many switches since the PS4 started - restart it first"); return; }
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
  if (own_spawns() >= OWN_SPAWN_MAX) { r2_slog("selfown: skipped - %d spawns since this PS4 started (limit %d)", own_spawns(), OWN_SPAWN_MAX); return; }
  own_spawn_add(); r2_slog("selfown: spawn %d since this PS4 started", own_spawns());
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
  look_key = key ? key : ""; /* 1.1.62: /_inplug.get downloads through the engine */
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
    { char jl[48]; snprintf(jl, sizeof jl, "job %.43s", t); r2_where(jl, 1); } /* 1.1.113: every job writes its own name */
    if (!strcmp(t, "/_profiles")) look_profiles();
    else if (!strncmp(t, "/_save.", 7)) look_save(t, dp);
    else if (!strncmp(t, "/_sback.", 8)) look_sback(key, t);
    else if (!strncmp(t, "/_srest.", 8)) look_srest(key, t);
    else if (!strncmp(t, "/_sdel.", 7)) look_sdel(key, t);
    else if (!strncmp(t, "/_sparts.", 9)) look_sparts(t);
    else if (!strncmp(t, "/_gback.", 8)) look_gback(key, t);
    else if (!strncmp(t, "/_gmove.", 8)) look_gmove(key, t);
    else if (!strncmp(t, "/_snap.", 7)) look_snap(key, t);
    else if (!strcmp(t, "/_close")) look_close();
    else if (!strncmp(t, "/_sdbinfo.", 10)) look_sdbinfo(t);
    else if (!strncmp(t, "/_avinfo.", 9)) look_avinfo(t);
    else if (!strncmp(t, "/_avcopy.", 9)) look_avcopy(t, 0);
    else if (!strncmp(t, "/_avreset.", 10)) look_avcopy(t, 1);
    else if (!strncmp(t, "/_avlib", 7)) look_avlib(key, t);
    else if (!strncmp(t, "/_rename.", 9)) look_rename(t); /* 1.1.100 */
    else if (!strcmp(t, "/_ghver")) look_ghver(); /* 1.1.103 */
    else if (!strcmp(t, "/_shprobe")) look_shprobe(key);
    else if (!strncmp(t, "/_shmod.", 8)) look_shmod(key, t); /* 1.1.109 */
    else if (!strncmp(t, "/_probeup.", 10)) look_probeup(key, t); /* 1.1.111 */
    else if (R2_SHELL && !strcmp(t, "/_shtest")) look_shtest(key); /* 1.1.115 test build */
    else if (!strcmp(t, "/_pwrreg")) look_pwrreg(); /* 1.1.111 */
    else if (!strcmp(t, "/_renamesi.on")) { put_file("/data/round2/rename-signedin", "1"); look_kv("renamesi/result", "member loads now rename signed-in spares"); }
    else if (!strcmp(t, "/_renamesi.off")) { unlink("/data/round2/rename-signedin"); look_kv("renamesi/result", "signed-in spares keep their names"); }
    else if (!strncmp(t, "/_udel.", 7)) look_udel(t);
    else if (!strncmp(t, "/_uorder.", 9)) look_uorder(t);
    else if (!strncmp(t, "/_clean.", 8)) look_clean(t);
    else if (!strncmp(t, "/_copy.", 7)) look_copy(t);
    else if (!strcmp(t, "/_copycheck")) look_copycheck();
    else if (!strcmp(t, "/_restoreall")) look_restoreall();
    else if (!strcmp(t, "/_tile.now")) tile_check(key, 1); /* 1.1.87 */
    else if (!strcmp(t, "/_tile.on")) { unlink("/data/round2/tile-off"); put_file("/data/round2/tile-on", "1"); look_kv("tile/result", "tile switched ON for this PS4"); } /* 1.1.99 */
    else if (!strcmp(t, "/_tile.off")) { unlink("/data/round2/tile-on"); put_file("/data/round2/tile-off", "1"); look_kv("tile/result", "tile switched off for this PS4"); }
    else if (!strncmp(t, "/_prepmember.", 13)) look_prepmember(t);
    else if (!strncmp(t, "/_memtag.", 9)) look_memtag(t);
    else if (!strcmp(t, "/_pf.off")) { put_file("/data/round2/prefetch-off", "1"); look_kv("pf/result", "prefetch OFF"); }
    else if (!strcmp(t, "/_pf.on")) { unlink("/data/round2/prefetch-off"); look_kv("pf/result", "prefetch ON"); }
    else if (!strcmp(t, "/_bb.off")) { put_file("/data/round2/bigbundle-off", "1"); look_kv("bb/result", "big-file bundles OFF"); }
    else if (!strcmp(t, "/_bb.on")) { unlink("/data/round2/bigbundle-off"); look_kv("bb/result", "big-file bundles ON"); }
    else if (!strcmp(t, "/_mi.off")) { put_file("/data/round2/mi-skip-off", "1"); look_kv("mi/result", "member-index skip OFF"); }
    else if (!strcmp(t, "/_mi.on")) { unlink("/data/round2/mi-skip-off"); look_kv("mi/result", "member-index skip ON"); }
    else if (!strncmp(t, "/_memload.", 10)) look_memload(key, t);
    else if (!strncmp(t, "/_memplay.", 10)) look_memplay(key, t); /* 1.1.90 */
    else if (!strncmp(t, "/_regscan.", 10)) look_regscan(t); /* 1.1.97 */
    /* 1.1.92: "/_padoff" removed - scePadDisconnectDevice hung the helper on Kaeden (1.1.91) */
    else if (!strcmp(t, "/_padoff.on")) { put_file("/data/round2/padoff-on", "1"); look_kv("padoff/auto", "on"); }
    else if (!strcmp(t, "/_padoff.off")) { unlink("/data/round2/padoff-on"); look_kv("padoff/auto", "off"); }
    else if (!strncmp(t, "/_purge.", 8)) look_purge(t);
    else if (!strncmp(t, "/_reset", 7)) look_reset(t);
    else if (!strncmp(t, "/_input.", 8)) look_input(t);
    else if (!strncmp(t, "/_inplug.", 9)) look_inplug(t);
    else if (!strncmp(t, "/_klog.", 7)) look_klog(t);
    else if (!strcmp(t, "/_timing")) { /* 1.1.69: name = count / avg / worst / last (ms) - keys start with tail/ so 300 chars survive */
      char k[64], v[120]; snprintf(v, sizeof v, "since %lld (%lld s ago)", g_tm_since, g_tm_since ? (long long)time(0) - g_tm_since : 0); look_kv("tail/timing since", v);
      for (int i = 0; i < TM_MAX && g_tm[i].name; i++) { snprintf(k, sizeof k, "tail/t %s", g_tm[i].name); snprintf(v, sizeof v, "%u x, avg %.0f ms, worst %.0f ms, last %.0f ms", g_tm[i].n, g_tm[i].sum / g_tm[i].n * 1000, g_tm[i].max * 1000, g_tm[i].last * 1000); look_kv(k, v); }
    }
    else if (!strncmp(t, "/_cloud.", 8)) look_cloud(t);
    else if (!strcmp(t, "/_rest")) look_rest();
    else if (!strcmp(t, "/_bpop.on")) { put_file(BPOP_FLAG, "1"); bp_on = 1; bp_chk = time(0); look_kv("bpop/result", "verbose pop-ups ON"); bpop("Verbose pop-ups ON - I will tell you what I am doing"); }
    else if (!strcmp(t, "/_bpop.off")) { unlink(BPOP_FLAG); bp_on = 0; look_kv("bpop/result", "verbose pop-ups off"); }
    else if (!strcmp(t, "/_bpop.tech")) { put_file("/data/round2/popups-tech", "1"); look_kv("bpop/result", "technical log lines shown as pop-ups too"); } /* 1.1.102 */
    else if (!strcmp(t, "/_bpop.plain")) { unlink("/data/round2/popups-tech"); look_kv("bpop/result", "plain pop-ups only"); }
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
  { /* 1.1.68 (owner 19 Nov 12:52am): the engine says how long to stay awake ("aw" seconds = 30 min before a booking until
       30 min after its guest end). Kept separately from a staff /_keepawake hold; the loop holds until the later of the two.
       Bounded: 0-12 h. No answer from the engine = the last hold simply runs out. */
    const char *a = strstr(body, "\"aw\":");
    if (a) { long long v = atoll(a + 5); if (v < 0) v = 0; if (v > 12 * 3600) v = 12 * 3600;
      long long until = v ? (long long)time(0) + v : 0;
      if ((until > 0) != (g_awake_engine > 0)) sv_log("booking keep-awake %s", until ? "ON" : "off");
      g_awake_engine = until; }
  }
  char *c = strstr(body, "\"cid\":\""), cid[24] = "";
  if (!c) return 0;
  c += 7;
  int i = 0;
  while (c[i] && c[i] != '"' && i < 23) { cid[i] = c[i]; i++; }
  cid[i] = 0;
  { char jp[120] = "", jd[16] = ""; jget(body, "do", jd, sizeof jd); jget(body, "p", jp, sizeof jp); bpop("Job from the control centre: %s %s", jd, jp); }
  if (strstr(body, "\"do\":\"clean\"")) { r2_where("job clean-up", 1); clean_up(key, cid); } /* 1.1.113: job names */
  else if (strstr(body, "\"do\":\"update\"")) { r2_where("job helper update", 1); do_update(key, cid, body); }
  else if (strstr(body, "\"do\":\"look\"")) do_look(key, cid, body);
  else if (strstr(body, "\"do\":\"prof\"")) { r2_where("job profile", 1); do_prof(key, cid, body); }
  else if (strstr(body, "\"do\":\"dns\"")) { r2_where("job blocker settings", 1); do_dns(key, cid, body); }
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

void r2_where_flush(double min_age);
static void on_sig(int sig) {
  r2_where_flush(0); /* 1.1.71 */
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
double r2_where_age(void); const char *r2_where_now(void); void r2_where_shift(double secs); void r2_where_flush(double min_age); void r2_where_alive(void);
static void wd_note(const char *fmt, ...) {
  FILE *f = fopen("/data/round2/slow.log", "a"); if (!f) return;
  time_t x = time(0) + 8 * 3600; struct tm tm; gmtime_r(&x, &tm);
  fprintf(f, "%02d/%02d %02d:%02d:%02d watchdog: ", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
  va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f);
}
/* 1.1.112 (owner 20 Nov 7:06am: "does the helper get busy on something like uploading reading or downloading and get stuck hence
   unable to respond to network calls or reach the engine?" – yes: check-ins, reports, every job, uploads, save copies, clean slate
   and wake jobs all run in ONE loop, so while a job runs the PS4 looked switched off). This thread does nothing while the loop
   checks in normally. Once the loop hasn't checked in for 90 s it tells the engine "on, busy with <step> for N s" (/ps4/alive), at
   most once a minute, with its own quiet request, and notes it in slow.log. Commands still wait for the loop. */
static volatile double g_loop_mono; /* last time the loop reached its check-in */
static char g_alive_key[128];
static void *alive_thread(void *arg) {
  (void)arg; double last_sent = 0, last_tick = mono_s(); int told = 0, sends = 0;
  for (;;) {
    sleep(20);
    double now = mono_s();
    if (now - last_tick > 60) { g_loop_mono = now; told = 0; } /* the PS4 was in rest: that's sleep, not busy */
    last_tick = now;
    if (!g_alive_key[0] || !g_loop_mono) continue;
    double gap = now - g_loop_mono;
    if (gap < 90) { if (told) wd_note("check-in thread: the loop is free again (%d busy check-ins sent)", sends); told = 0; sends = 0; continue; }
    if (now - last_sent < 60) continue;
    last_sent = now;
    char st[48], hdr[220], req[300], body[200], err[120];
    snprintf(st, sizeof st, "%s", r2_where_now());
    for (char *q = st; *q; q++) if (*q == '"' || *q == '\\' || (unsigned char)*q < 32) *q = ' ';
    snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", g_alive_key);
    int n = snprintf(req, sizeof req, "{\"id\":\"%s\",\"v\":\"%s\",\"busy\":\"%s\",\"stepS\":%d,\"loopS\":%d}", console_id, R2_VERSION, st, (int)r2_where_age(), (int)gap);
    int r = r2tls_request_quiet(ENGINE_HOST, "POST", "/ps4/alive", hdr, req, n, body, sizeof body, err, sizeof err);
    sends++;
    if (!told) { told = 1; wd_note("busy %d s in '%s' - check-in thread told the engine the PS4 is on (%d%s%s)", (int)gap, st, r, err[0] ? " " : "", err); }
  }
  return 0;
}
/* 1.1.124 (owner 9:02pm: after the tile closed he could move in the menu ~4 s before the log-out): fg.txt (what's in front, for
   the menu add-on) is written within 0.25 s while a member load runs or just finished (else every 2 s); the pop-up the moment
   the tile is gone. */
static void *fg_thread(void *arg) {
  (void)arg; char last[32] = "-";
  for (;;) {
    int hot = g_after_load || file_exists("/data/round2/memqn");
    char rt[32]; running_title(rt, sizeof rt);
    if (strcmp(rt, last)) { snprintf(last, sizeof last, "%s", rt); put_file("/data/round2/fg.txt", rt); }
    if (g_after_load && (strcmp(rt, "RTWO00001") || time(0) - g_after_load > 20)) {
      int game = rt[0] && strcmp(rt, "RTWO00001") && strcmp(rt, VUE_ID);
      notify("Saves loaded! Press the PS button and pick your profile.");
      sv_log("after the member load: %s%s - pop-up shown", game ? "a game was opened, it closes with the log-out: " : strcmp(rt, "RTWO00001") ? "tile closed / not open" : "tile still open after 20 s", game ? rt : "");
      g_after_load = 0; }
    usleep(hot ? 250000 : 2000000);
  }
  return 0;
}
static void *watchdog(void *arg) {
  (void)arg; int cuts = 0, noted = 0; double last = mono_s();
  for (;;) {
    sleep(15);
    /* 1.1.70: this loop wakes every 15 s. A much longer gap means the PS4 was in rest mode (every thread stands still, the
       running clock doesn't) – that time is sleep, not a freeze: take it off the step's age. Before this, Bella (the only PS4
       running the helper as its own program) restarted its helper on every wake after 20+ min of rest, and if the starter's
       relaunch failed right after the wake the room went silent while the PS4 was on. */
    { double t = mono_s(), slept = t - last - 15; last = t;
      if (slept > 30) { r2_where_shift(slept); cuts = 0; noted = 0; wd_note("woke from rest after about %d s - not counted as stuck", (int)slept); bpop("Woke up after %d min %d s asleep - carrying on", (int)slept / 60, (int)slept % 60); } }
    r2_where_flush(10); /* 1.1.71: where.txt only for a step that lasts */
    { static double la = 0; double t = mono_s(); if (t - la >= 60) { la = t; r2_where_alive(); } } /* 1.1.114: last minute alive */
    double age = r2_where_age(); const char *st = r2_where_now();
    if (age < 300) { cuts = 0; noted = 0; continue; }
    int net = !strcmp(st, "read") || !strcmp(st, "send") || !strcmp(st, "connect") || !strcmp(st, "handshake") || !strcmp(st, "lookup");
    int fd = r2tls_fd;
    if (net && fd >= 0 && cuts < 3) { shutdown(fd, SHUT_RDWR); cuts++; wd_note("stuck %d s in '%s' - cut the connection (%d)", (int)age, st, cuts); continue; }
    if (age >= 1200 && g_own && !sv_mounted) { wd_note("stuck %d s in '%s' - restarting the helper", (int)age, st); r2_where_flush(0); _exit(3); }
    if (!noted && age >= 600) { noted = 1; wd_note("stuck %d s in '%s'%s", (int)age, st, g_own ? "" : " (shared program: can't restart itself)"); }
  }
  return 0;
}
/* 1.1.87 (owner 19 Nov 11:30pm: "teach the helper to update the Round2 tile – I don't want to go back and forth"): the ROUND2
   TILE (fPKG RTWO00001) installs and updates itself through the helper. Every 30 min (and 2 min after start, and on the staff
   look "/_tile.now"): ask the engine which tile version is out (GET /ps4/tileinfo); if this PS4 doesn't have it yet, download
   it (POST /ps4/tilepkg, size + SHA-256 checked) to /data/pkg/round2-tile.pkg and hand it to the PS4's own background
   installer (BGFT, the same one GoldHEN's Package Installer and "Internal PKG Installer" use). Never while a game or the tile
   itself is open, never during a clean slate / backup. If the installer refuses, the file stays in /data/pkg so staff can
   install it with Debug Settings > Package Installer (hdd), and the same version isn't tried again until the next restart.
   /data/round2/tile.ver = the version installed; the report carries it ("tile").
   1.1.88: the PS4 answers 0x80990088 ("already installed") for a newer tile over an older one -> remove the old one, install again. */
#define TILE_PKG "/data/pkg/round2-tile.pkg"
static char tile_tried[24];
typedef struct { void *heap; size_t heapSize; } BgftInit;
typedef struct { int32_t userId; int32_t entitlementType; const char *id; const char *contentUrl; const char *contentExUrl; const char *contentName;
  const char *iconPath; const char *skuId; int32_t option; const char *playgoScenarioId; const char *releaseDate; const char *packageType;
  const char *packageSubType; uint32_t packageSize; } BgftParam;
typedef struct { BgftParam params; uint32_t slot; } BgftParamEx;
static int tile_install(char *why, size_t wn) {
  static int (*bInit)(BgftInit *), (*bReg)(BgftParamEx *, int *), (*bStart)(int); static int looked, inited, mh; static void *heap;
  if (!looked) { looked = 1; mh = sceKernelLoadStartModule("/system/common/lib/libSceBgft.sprx", 0, 0, 0, 0, 0);
    if (mh > 0) { sceKernelDlsym(mh, "sceBgftServiceIntInit", (void **)&bInit); sceKernelDlsym(mh, "sceBgftServiceIntDownloadRegisterTaskByStorageEx", (void **)&bReg); sceKernelDlsym(mh, "sceBgftServiceDownloadStartTask", (void **)&bStart); } }
  if (!bInit || !bReg || !bStart) { snprintf(why, wn, "the PS4 installer isn't reachable from the helper (module 0x%08x)", (unsigned)mh); return -1; }
  if (!inited) { if (!heap) heap = calloc(1, 1 << 20); BgftInit ip = { heap, 1 << 20 }; int r = bInit(&ip); if (r) { snprintf(why, wn, "installer start 0x%08x", (unsigned)r); return -1; } inited = 1; }
  BgftParamEx p; memset(&p, 0, sizeof p);
  p.params.entitlementType = 5; p.params.id = ""; p.params.contentUrl = "/user" TILE_PKG; p.params.contentName = "Round2"; p.params.iconPath = "";
  p.params.playgoScenarioId = "0"; p.params.option = 0x10000; p.slot = 0;
  int task = -1, r = bReg(&p, &task);
  if (r == (int)0x80990088) { /* 1.1.88: "already installed" (DirectPackageInstaller's reading of this code) – a newer tile can't be
       laid over the old one, so the old one is removed first (the tile keeps nothing of its own), then installed again */
    static int (*aInit)(void), (*aUninst)(const char *); static int alooked;
    if (!alooked) { alooked = 1; int am = sceKernelLoadStartModule("/system/common/lib/libSceAppInstUtil.sprx", 0, 0, 0, 0, 0);
      if (am > 0) { sceKernelDlsym(am, "sceAppInstUtilInitialize", (void **)&aInit); sceKernelDlsym(am, "sceAppInstUtilAppUnInstall", (void **)&aUninst); } }
    if (!aInit || !aUninst) { snprintf(why, wn, "old tile in the way and the remover isn't reachable"); return -1; }
    int ri = aInit(), ru = aUninst("RTWO00001"); sv_log("tile: old one removed first (init 0x%08x, remove 0x%08x)", (unsigned)ri, (unsigned)ru);
    sleep(3); task = -1; r = bReg(&p, &task);
  }
  if (r) { snprintf(why, wn, "installer said 0x%08x", (unsigned)r); return -1; }
  r = bStart(task); if (r) { snprintf(why, wn, "install start 0x%08x (task %d)", (unsigned)r, task); return -1; }
  snprintf(why, wn, "install started (task %d)", task); return 0;
}
static void tile_ver(char *v, size_t n) { v[0] = 0; FILE *f = fopen("/data/round2/tile.ver", "r"); if (f) { if (fgets(v, (int)n, f)) v[strcspn(v, "\r\n")] = 0; fclose(f); } }
static int tile_check(const char *key, int force) {
  char run[32], have[24], v[24], sha[80], sz[24], hdr[200], err[160], why[200] = ""; running_title(run, sizeof run);
  if (snap_on || clean_on) return 0;
  /* 1.1.99 (owner 20 Nov 1:59am, moving Max: "doesn't install ps4 tile, just the major fixes"): the tile is only installed/updated
     where it's switched on – /data/round2/tile-on ("/_tile.on"), or already installed (Kaeden). "/_tile.off" stops it. */
  { char hv[24]; tile_ver(hv, sizeof hv); if (file_exists("/data/round2/tile-off") || (!hv[0] && !file_exists("/data/round2/tile-on"))) { if (force) look_kv("tile/result", "the tile is switched off on this PS4 (/_tile.on to allow)"); return 0; } }
  if (run[0] && !strcmp(run, "RTWO00001")) { char cr[160]; if (close_game(cr, sizeof cr)) { if (force) look_kv("tile/waits", cr); return 0; } running_title(run, sizeof run); } /* 1.1.90: the tile is closed first (owner: no closing by hand) */
  if (run[0] && strcmp(run, VUE_ID)) { if (force) look_kv("tile/waits", run); return 0; }   /* a game is open */
  snprintf(hdr, sizeof hdr, "x-r2-ps4: %s\r\nContent-Type: application/json\r\n", key);
  unsigned char *d = 0; size_t len = 0; int st = r2tls_fetch(ENGINE_HOST, "GET", "/ps4/tileinfo", hdr, "", 0, &d, &len, 4096, err, sizeof err);
  if (st != 200 || !d) { free(d); if (force) { snprintf(why, sizeof why, "engine %d %s", st, err); look_kv("tile/error", why); } return 0; }
  { char *b = malloc(len + 1); if (!b) { free(d); return 0; } memcpy(b, d, len); b[len] = 0; jget(b, "v", v, sizeof v); jget(b, "sha", sha, sizeof sha); jget(b, "size", sz, sizeof sz); free(b); free(d); d = 0; }
  tile_ver(have, sizeof have);
  if (!v[0] || strlen(sha) != 64) { if (force) look_kv("tile/result", "no tile release yet"); return 0; }
  if (!force && (!strcmp(v, have) || !strcmp(v, tile_tried))) return 0;
  snprintf(tile_tried, sizeof tile_tried, "%s", v);
  bpop_key("tile", "Round2 tile %s: downloading...", v);
  int n = snprintf(err, sizeof err, "{\"id\":\"%s\",\"v\":\"%s\"}", console_id, v);
  char body[160]; snprintf(body, sizeof body, "%.*s", n, err);
  mkdir("/data/pkg", 0777);
  { /* 1.1.120: straight into the file (the 4 MB memory buffer couldn't hold tile 0.08), then size + SHA-256 from the file */
    FILE *f = fopen(TILE_PKG ".tmp", "wb"); size_t got = 0;
    if (!f) snprintf(why, sizeof why, "couldn't save it");
    else { st = r2tls_fetch_file(ENGINE_HOST, "POST", "/ps4/tilepkg", hdr, body, strlen(body), f, &got, err, sizeof err); fflush(f); fsync(fileno(f)); fclose(f);
      if (st != 200) snprintf(why, sizeof why, "download failed (%d %s)", st, err);
      else if (got != (size_t)atoi_safe(sz)) snprintf(why, sizeof why, "download size %zu, expected %s", got, sz);
      else { unsigned char h[32]; char hx[65]; mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
        FILE *g = fopen(TILE_PKG ".tmp", "rb"); static unsigned char buf[65536]; size_t k;
        if (g) { while ((k = fread(buf, 1, sizeof buf, g)) > 0) mbedtls_sha256_update(&c, buf, k); fclose(g); }
        mbedtls_sha256_finish(&c, h); mbedtls_sha256_free(&c);
        for (int i = 0; i < 32; i++) sprintf(hx + i * 2, "%02x", h[i]);
        if (!g || strcasecmp(hx, sha)) snprintf(why, sizeof why, "file check failed (SHA-256)"); }
      if (!why[0] && rename(TILE_PKG ".tmp", TILE_PKG)) snprintf(why, sizeof why, "couldn't save it (rename)"); } }
  if (why[0]) { unlink(TILE_PKG ".tmp"); sv_log("tile %s: %s", v, why); bpop_key("tile", "Round2 tile %s not updated: %s", v, why); if (force) look_kv("tile/error", why); return 0; }
  int r = tile_install(why, sizeof why);
  sv_log("tile %s (had '%s'): %s", v, have, why);
  if (!r) { put_file("/data/round2/tile.ver", v); notify("Round2 tile updated - open it from the home screen."); }
  else notify("New Round2 tile ready: Settings > Debug Settings > Package Installer (hdd) to install it.");
  bpop_key("tile", "Round2 tile %s: %s", v, why);
  if (force) look_kv(r ? "tile/error" : "tile/result", why);
  return 1;
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
  /* 1.1.73: "hello" (the starter keeps a new version once it says it) moved to after the first report – so a version that
     crashes in its first secure connection is rolled back by the starter instead of kept. */
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
  if (console_id[0]) { mkdir("/data/round2", 0777); put_file("/data/round2/tile-id.txt", console_id); } /* 1.1.89: the Round2 tile reads this PS4's id here (apps can't ask the kernel for it) */
  char key[128] = "", status[400] = "starting";
  int warned = 0;
  g_up = (long long)time(0);
  drift0 = (long long)time(0) - (long long)mono_s();
  write_status("started, first report in about 10 seconds");
  r2_where_start(R2_VERSION); /* 1.1.26: slow.log gets "the run before was last in: <step>" */
  { /* 1.1.114 (owner 20 Nov, Kai: switched off or crashed?): the PS4's own power notes at every start, read-only - compared over
       known events (rest, menu power-off, power button held, power cut) they should tell a switch-off from a crash */
    int ph = -1, ss = -1; unsigned char sb[8] = { 0 }; unsigned long long sbv = 0;
    int r1 = sceRegMgrGetInt(0x2820b00, &ph), r2 = sceRegMgrGetInt(0x2820e00, &ss), r3 = sceRegMgrGetBin(0x2820900, sb, 8);
    for (int j = 7; j >= 0; j--) sbv = (sbv << 8) | sb[j];
    FILE *f = fopen("/data/round2/slow.log", "a");
    char ts[24]; { time_t x = time(0) + 8 * 3600; struct tm tm; gmtime_r(&x, &tm); strftime(ts, sizeof ts, "%d/%m %H:%M:%S", &tm); }
    if (f) { fprintf(f, "%s power notes at start: power_history %d%s, shutdown_status %d%s, standby_start %llu%s\n", ts, ph, r1 ? "(err)" : "", ss, r2 ? "(err)" : "", sbv, r3 ? "(err)" : ""); fclose(f); }
  }
  { pthread_t wt; if (pthread_create(&wt, 0, watchdog, 0) == 0) pthread_detach(wt); else wd_note("could not start"); } /* 1.1.42 */
  { pthread_t bt; if (pthread_create(&bt, 0, bpop_thread, 0) == 0) pthread_detach(bt); } /* 1.1.75 */
  { pthread_t at; if (pthread_create(&at, 0, alive_thread, 0) == 0) pthread_detach(at); else wd_note("check-in thread could not start"); } /* 1.1.112 */
  if (R2_SHELL) { pthread_t ft; if (pthread_create(&ft, 0, fg_thread, 0) == 0) pthread_detach(ft); } /* 1.1.124 */
  bpop("Helper started - connecting to Round2...");
  update_guard();
  { char st[16] = ""; FILE *f = fopen(SNAPD "/state", "r"); if (f) { if (fgets(st, sizeof st, f) && (!strncmp(st, "running", 7) || !strncmp(st, "paused", 6))) snap_on = 1; fclose(f); } } /* 1.1.41: carry on a snapshot */
  { char st[16] = ""; cl_rd("state", st, sizeof st); clean_on = !strcmp(st, "running") || !strcmp(st, "paused"); } /* 1.1.50: carry on a clean slate */
  cd_load(); /* 1.1.55: carry on a reset countdown */
  { FILE *ka = fopen("/data/round2/keepawake.txt", "r"); char b[24] = ""; if (ka) { if (fgets(b, sizeof b, ka)) { long long u = atoll(b); if (u > (long long)time(0)) g_awake_until = u; } fclose(ka); } } /* 1.1.74 */
  for (unsigned long m = 0;; m++) {
    char run[32];
    double t_hk = tm_now(); /* 1.1.69: the once-a-minute housekeeping below */
    set_dns_servers(); /* 1.1.26: our own name lookups ask the PS4's DNS servers first */
    r2_where("title", 0);
    running_title(run, sizeof run);
    r2_where("playing", 0);
    note_playing(run);
    r2_where("loop", 0);
    FILE *a = fopen("/data/round2/alive", "w"); /* for the starter (later) */
    if (a) { fprintf(a, "%lu\n", m); fclose(a); }
    r2_timing("minute housekeeping", tm_now() - t_hk);
    if (m % REPORT_EVERY == 0) { /* 1.0.5: first report ~10 s after start (was 1 min), retried while the network settles */
      if (!read_key(key, sizeof key)) {
        write_status("no key – put the PS4 key in /data/round2/key.txt");
        if (!warned++) notify("Round2 helper: key missing (/data/round2/key.txt)");
        if (m == 0) put_file(APP "/hello", R2_VERSION); /* 1.1.73 */
      } else {
        if (m == 0) sleep(10);
        int st = report(key, status, sizeof status);
        bpop_key("rep", st == 200 ? "Report sent to Round2 (games, disk, Wi-Fi) - OK" : "Report to Round2 FAILED (%d) - will retry", st);
        for (int i = 0; m == 0 && st != 200 && i < 6; i++) { sleep(8); st = report(key, status, sizeof status); }
        write_status(status);
        if (m == 0) put_file(APP "/hello", R2_VERSION); /* 1.1.73 */
        if (m == 0) {
          char msg[200];
          snprintf(msg, sizeof msg, "Round2 helper %s: %s", R2_VERSION, st == 200 ? "connected" : "can't reach Round2 (will retry)");
          notify(msg);
        }
        if (m == 0 && st == 200 && !g_own && (fw_major() == 9 || fw_major() == 0x11 || fw_major() == 0x13)) { /* 1.1.83: + 13.50 (Kaeden) */ /* 1.1.32: become our own program so later updates don't pile up sleeping copies.
             9.00 since 1.1.32; 11.00 (sdk 0x11) since 1.1.60 - proven on Bella 18 Nov 3:03-3:15am: own switch, own->own update
             (1.1.58->1.1.59, old program gone), rest + wake by PS button (same program carried on).
             Gated: at most one attempt per hour (marker file), never while a game is open (1.1.60), and a failed attempt just
             leaves us running the normal way. */
          char rt[32]; running_title(rt, sizeof rt);
          struct stat ms; int recent = !stat(APP "/promote-try", &ms) && (long long)time(0) - ms.st_mtime < 3600;
          if (!recent && !(rt[0] && strcmp(rt, VUE_ID))) { put_file(APP "/promote-try", R2_VERSION); selfown_after = 1; }
        }
      }
    }
    if (g_awake_until) { if ((long long)time(0) < g_awake_until) { sceSystemServicePowerTick(); sceShellCoreUtilResetAutoPowerDownTimer(); } else g_awake_until = 0; }
    if (g_awake_engine) { if ((long long)time(0) < g_awake_engine) { sceSystemServicePowerTick(); sceShellCoreUtilResetAutoPowerDownTimer(); } else g_awake_engine = 0; }
    if (selfown_after) { selfown_after = 0; sleep(2); do_selfown(); } /* 1.1.33: act on auto-promote here, not only after a look job */
    r2_where("check-in", 1);
    g_loop_mono = mono_s(); if (key[0] && !g_alive_key[0]) snprintf(g_alive_key, sizeof g_alive_key, "%s", key); /* 1.1.112 */
    { double t = tm_now(); int did = (key[0] || read_key(key, sizeof key)) && check_cmd(key); r2_timing(did ? "check-in + doing a job" : "check-in (nothing to do)", tm_now() - t); /* 1.1.71: split */
      if (!did && m % 3 == 0) bpop_key("hb", "Online - checked in (%.2f s), no jobs. %s%s", tm_now() - t, snap_on ? "Backup running. " : clean_on ? "Copy/clean running. " : "", g_awake_until > (long long)time(0) ? "Staying awake." : "");
      if (did) { report(key, status, sizeof status); write_status(status); } }
    if (key[0] && (m == 2 || (m && m % 30 == 0))) { r2_where("tile", 1); tile_check(key, 0); } /* 1.1.87: Round2 tile updates */
    if (!clean_on && !snap_on) keep_order(0); /* 1.1.101: main, Player 1-4 stay in order */
    if (key[0]) { r2_where("save watch", 1); sw_tick(key); } /* 1.1.102: save when the game closes */
    if (key[0]) defer_tick(key); /* 1.1.123: pre-load backups to the cloud after the load */
    if (R2_SHELL && key[0]) { r2_where("job menu add-on", 1); shell_auto(key); } /* 1.1.115 (test build only): Round2 menu add-on at every PS4 start */
    if (snap_on && key[0]) { r2_where("snapshot", 1); snap_step(key, 45); } /* 1.1.41: ~45 s of backup per minute */
    else if (clean_on && key[0]) { r2_where("clean", 1); clean_step(key, 45); } /* 1.1.50: ~45 s of clean slate per minute */
    else if (key[0] && file_exists(PREPD "/cloud_on")) { r2_where("cloud", 1); cloud_step(key, 20); } /* 1.1.52: background cloud batch, off by default */
    for (int t = 0; t < (snap_on || clean_on ? 5 : 30); t++) { /* one minute = 30 x 2 s; the update guard runs every 2 s */
      if (file_exists(APP "/stop")) step_aside(); /* the starter asked (roll-back) */
      r2_where("guard", 0);
      countdown_tick(key); /* 1.1.55: reset pop-ups + the engine's last check, every 2 s */
      load_watch(); /* 1.1.108: someone signs in during a member load -> "hang on" */
      /* 1.1.124: "what's in front" + the after-load pop-up moved to fg_thread (every 0.25 s while it matters) */
      if (t % 3 == 2 && key[0]) { char rt2[32]; running_title(rt2, sizeof rt2); /* 1.1.95 (owner: 20-30 s from "Load" to the PS4 starting): while the */
        if (!strcmp(rt2, "RTWO00001") && check_cmd(key)) { report(key, status, sizeof status); write_status(status); } } /* Round2 tile is open, look for jobs every 6 s */
      inp_watch(); /* 1.1.64: test pop-ups for the detector (only while switched on) */
      double tug = tm_now(); int ug = update_guard(); r2_timing("update guard (every 2 s)", tm_now() - tug);
      if (ug) {
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
