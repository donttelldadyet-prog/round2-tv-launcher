// round2_shell – Round2 add-on inside the PS4's system menu (SceShellUI). Owner, 20 Nov 2026 3:34–5:06am:
//   "once saves are loaded … logout, so that players can reselect their profiles seamlessly"; "a controller lock until after the
//   profiles are set: ps button is locked … unlock button combo like L3 L2 R3 R2"; worry: "turning on a controller would call the
//   profile picker screen which would clash".
// Uses ONLY the menu's own functions, found by round2_shprobe 1.00–1.03 (read-only):
//   Sce.Vsh.ShellUI.LoginMgrUtil.SuspendLoginScreen() / ResumeLoginScreen() / IsSuspendedLoginScreen()  – the profile picker
//   Sce.Vsh.ShellUI.PadInternal.Locker(LockType lock, LockType uiMask) + DisposableObj.Dispose()          – PS/Share button lock
//   Sce.Vsh.ShellUI.LoginMgrUtil.LogoutAll(LogoutMethodType)  (13 = LogoutFromPsImposeMenuV400, the PS menu's own Log Out)
//   PadManager.Instance.m_padInfo[i].m_curGeneralButtons (GamePadButtons: L2 0x1000, R2 0x2000, L3 0x4000, R3 0x8000)
// 8.00 (20 Nov 8:50pm): waits at most 20 s for the tile (was 40 – the 8:36pm test waited all 40 s for a tile that never closed).
// 7.00 (owner 20 Nov 8:05pm): log out "immediately after close of ps4 tile", and never while a game is open. After a load the
//   add-on waits (max 40 s) until the helper's /data/round2/fg.txt (what's in front: "" / RTWO00001 / a game) no longer says the
//   tile, waits 2 s for the helper's pop-up ("press the PS button and pick your profile"), then logs everyone out – ALSO if a game
//   was opened in those seconds (owner 8:20pm: "just close" – saves never load while a game is open, so nothing clashes; the game
//   closes, the guest picks their own profile and opens it again with the loaded saves). Tile 0.08 closes itself after "Ready".
// 6.00 (20 Nov 7:35pm): self-test #2 showed m_curGeneralButtons IS live (d-pad 0x1/0x2/0x4/0x8, square 0x10) but it is PSM's
//   simplified list – no L2/R2/L3/R3, so the combo could never match. 6.00 also reads each slot's m_curButtons (Orbis SystemButtons,
//   the raw reading) and logs "gen 0x.. sys 0x.." on every change in the self-test, to learn the raw numbers of L2/R2/L3/R3.
//   Same proven Mono functions only.
// 5.00 (20 Nov 7:25pm, after 4.00 crashed Kaeden's menu on load at 7:10pm): round2_padprobe1 (read-only, 7:23pm) showed m_padInfo
//   IS a List<PadInfo> of objects (_items PadInfo[32], _size 24) – shell3's read failed only because it refused more than 8 entries.
//   5.00 drops the 5 Mono imports 4.00 added (never used inside the menu before – the likely crash), reads up to 32 entries with
//   proven functions only, and keeps 4.00's self-test-on-request + shell.alive. Rule: only Mono functions proven by an earlier run.
// 4.00 (owner 20 Nov 3:16pm, after the Kaeden restart): (1) the button read: 3.00 read "can't read" (0xffffffff) at 8:09am
//   although the pad fields were found (pads 1/1/1) - so m_padInfo is not the List<> 3.00 assumed. 4.00 writes ONE description of it
//   (type, array or list, element type, struct or object, size, PadInfo's own fields) and reads arrays and lists, of objects or
//   structs; "no controller" now = 0, -1 only when it really can't read. (2) the helper loads this add-on at every PS4 start
//   (helper 1.1.114), so the 60-s SELF-TEST only runs when asked: file /data/round2/shell-selftest (helper "/_shtest"), deleted
//   when it starts; during it every change of the buttons is logged, so nobody has to time a press. (3) /data/round2/shell.alive =
//   time of the last loop (every 30 s) - the helper skips loading again while a live add-on of this name is running.
// 3.00 (owner 20 Nov 8:01am, decision 4 "definitely implement": guests are greeted by the profile picker after the tile has loaded
//   their saves): after EVERY member load, 8 s after it finishes (the tile shows "Ready"), the menu's own Log Out for everyone
//   (LogoutAll 13) – no switch file needed any more (/data/round2/shell-nologout turns it off). The self-test ends with the same
//   log-out, so the owner sees where a PS4 lands (profile picker?) with and without a controller connected.
// 2.00 (20 Nov 7:50am): finds the loaded app.exe like round2_sweep2 did on all 6 PS4s; a "calling: X" breadcrumb line BEFORE every
//   call into the menu (pwrprobe1 crash lesson). The calls it uses exist identically on 9.00 / 11.00 / 13.50 (sweep 20 Nov).
// 1.00 = SELF-TEST + WATCH:
//   self-test (once, on load): picker paused + PS/Share locked for 60 s (or until L2+R2+L3+R3 held 1 s), then everything back.
//   watch: while the Round2 helper is loading member saves (/data/round2/memqn exists or the clean job is a running "prep"):
//   picker paused + PS/Share locked (combo still unlocks; a lock never lasts more than 10 min); released when the load ends.
//   Log-out after a load is NOT automatic in 1.00 – only when /data/round2/shell-logout exists (helper switch, later).
// Every call catches the menu's own errors (mono_runtime_invoke with an exception slot) – nothing is patched in memory.
// Log: /data/round2/round2_shellN.txt (as /user/data/... – the menu can't see /data). Newer add-on → older ones stop (shell.gen).
#include "plugin_common.h"
#include "Common.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>

#define R2SH_VER "8.00"
#define R2SH_NAME "round2_shell8"
attr_public const char *g_pluginName = R2SH_NAME;
attr_public const char *g_pluginDesc = "Round2: profile picker pause, PS button lock, proper log-out";
attr_public const char *g_pluginAuth = "Round2";
attr_public u32 g_pluginVersion = 0x00000800;

extern void *mono_get_root_domain(void);
extern void *mono_thread_attach(void *);
extern void *mono_domain_assembly_open(void *, const char *);
extern void *mono_assembly_get_image(void *);
extern void *mono_class_from_name(void *, const char *, const char *);
extern void *mono_class_get_method_from_name(void *, const char *, int);
extern void *mono_class_get_parent(void *);
extern void *mono_class_get_field_from_name(void *, const char *);
extern void mono_field_get_value(void *, void *, void *);
extern void *mono_runtime_invoke(void *, void *, void **, void **);
extern void *mono_object_new(void *, void *);
extern unsigned int mono_gchandle_new(void *, int);
extern void mono_gchandle_free(unsigned int);
extern void *mono_object_get_class(void *);
extern char *mono_array_addr_with_size(void *, int, uintptr_t);
extern uintptr_t mono_array_length(void *);
extern void *mono_object_to_string(void *, void **);
extern char *mono_string_to_utf8(void *);
extern const char *mono_class_get_name(void *);

#define DIR_U "/user/data/round2"
static char g_log[96];
static void lg(const char *fmt, ...) {
  char b[400]; long t = ((long)time(0) + 8 * 3600) % 86400;
  int n = snprintf(b, sizeof b, "%02ld:%02ld:%02ld ", t / 3600, (t / 60) % 60, t % 60);
  va_list ap; va_start(ap, fmt); n += vsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap); if (n > (int)sizeof b - 2) n = sizeof b - 2; b[n++] = '\n';
  int fd = open(g_log, O_WRONLY | O_CREAT | O_APPEND, 0666); if (fd >= 0) { write(fd, b, n); close(fd); }
}
static int exists_(const char *p) { struct stat st; return !stat(p, &st); }
static void rd_(const char *p, char *b, int n) { b[0] = 0; int fd = open(p, O_RDONLY); if (fd < 0) return; int k = read(fd, b, n - 1); close(fd); if (k < 0) k = 0; b[k] = 0; for (int i = 0; b[i]; i++) if (b[i] == '\n' || b[i] == '\r') { b[i] = 0; break; } }
static void put_(const char *p, const char *s) { int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (fd >= 0) { write(fd, s, strlen(s)); close(fd); } }

static void *g_dom, *g_img, *mSuspend, *mResume, *mIsSusp, *mLogoutAll, *cLocker, *mLockerCtor, *mDispose, *mPadInst, *fPadInfo, *fButtons, *fSysButtons;
static unsigned long long g_sys;   /* 6.00: OR of every slot's m_curButtons (raw), set by held_buttons() */
static int g_quiet_calls; /* 2.00: the button reads every few 100 ms don't get a breadcrumb line each */
static int inv(const char *what, void *m, void *obj, void **args, void **ret) {   /* 0 = ok; the menu's own error is logged, never thrown */
  if (!g_quiet_calls) lg("calling: %s", what);   /* 2.00 (20 Nov, after the pwrprobe1 crash): breadcrumb BEFORE every call into the menu */
  void *exc = 0; void *r = m ? mono_runtime_invoke(m, obj, args, &exc) : 0; if (ret) *ret = r;
  if (!m) { lg("%s: function not found", what); return -1; }
  if (exc) { void *e2 = 0; void *s = mono_object_to_string(exc, &e2); char *u = s && !e2 ? mono_string_to_utf8(s) : 0; lg("%s: the menu said no: %.300s", what, u ? u : "(exception)"); return -1; }
  return 0;
}
extern void mono_assembly_foreach(void (*)(void *, void *), void *);
extern void *mono_image_get_name(void *);
static void find_app(void *as, void *ud) { (void)ud; if (g_img) return; void *im = mono_assembly_get_image(as); const char *n = im ? (const char *)mono_image_get_name(im) : 0; if (n && !strcmp(n, "app")) g_img = im; }
static int resolve(void) {
  g_dom = mono_get_root_domain(); if (!g_dom) { lg("no menu runtime"); return -1; }
  mono_thread_attach(g_dom);
  /* 2.00: find the ALREADY LOADED app.exe the way round2_sweep2 did on all 6 PS4s (mono_assembly_foreach, image name "app") –
     instead of asking Mono to open a path (could load a second copy) */
  mono_assembly_foreach(find_app, 0); if (!g_img) { lg("app.exe not found among the loaded assemblies"); return -1; }
  void *cl = mono_class_from_name(g_img, "Sce.Vsh.ShellUI", "LoginMgrUtil");
  if (cl) { mSuspend = mono_class_get_method_from_name(cl, "SuspendLoginScreen", 0); mResume = mono_class_get_method_from_name(cl, "ResumeLoginScreen", 0);
    mIsSusp = mono_class_get_method_from_name(cl, "IsSuspendedLoginScreen", 0); mLogoutAll = mono_class_get_method_from_name(cl, "LogoutAll", 1); }
  cLocker = mono_class_from_name(g_img, "Sce.Vsh.ShellUI.PadInternal", "Locker");
  if (cLocker) { mLockerCtor = mono_class_get_method_from_name(cLocker, ".ctor", 2); void *p = mono_class_get_parent(cLocker); if (p) mDispose = mono_class_get_method_from_name(p, "Dispose", 0); }
  void *pm = mono_class_from_name(g_img, "Sce.Vsh.ShellUI.PadInternal", "PadManager"); if (pm) { mPadInst = mono_class_get_method_from_name(pm, "get_Instance", 0); fPadInfo = mono_class_get_field_from_name(pm, "m_padInfo"); }
  void *pi = mono_class_from_name(g_img, "Sce.Vsh.ShellUI.PadInternal", "PadInfo"); if (pi) { fButtons = mono_class_get_field_from_name(pi, "m_curGeneralButtons"); fSysButtons = mono_class_get_field_from_name(pi, "m_curButtons"); }
  lg("found: suspend %d resume %d is-suspended %d logout-all %d locker %d/%d/%d pads %d/%d/%d", !!mSuspend, !!mResume, !!mIsSusp, !!mLogoutAll, !!cLocker, !!mLockerCtor, !!mDispose, !!mPadInst, !!fPadInfo, !!fButtons);
  return 0;
}
extern void *mono_object_unbox(void *);
static int suspended(void) { void *r = 0; if (inv("is-suspended", mIsSusp, 0, 0, &r) || !r) return -1; return *(unsigned char *)mono_object_unbox(r) ? 1 : 0; }
/* 5.00: buttons held on any controller (OR of all). m_padInfo = List<PadInfo> (objects), seen with 24 entries (padprobe1, 20 Nov
   7:23pm) – read up to 32. 0 = none held / no controller; -1 = can't read (why -> g_padwhy). Proven Mono functions only. */
static char g_padwhy[200];
static int g_paddesc = 0;
static int held_buttons(void) {
  if (!mPadInst || !fPadInfo || !fButtons) { snprintf(g_padwhy, sizeof g_padwhy, "pad functions not found"); return -1; }
  void *pmi = 0; g_quiet_calls = 1; int bad = inv("pads", mPadInst, 0, 0, &pmi); g_quiet_calls = 0;
  if (bad || !pmi) { snprintf(g_padwhy, sizeof g_padwhy, "PadManager.Instance gave nothing"); return -1; }
  void *v = 0; mono_field_get_value(pmi, fPadInfo, &v); if (!v) { snprintf(g_padwhy, sizeof g_padwhy, "m_padInfo is empty (null)"); return -1; }
  void *vc = mono_object_get_class(v), *fi = vc ? mono_class_get_field_from_name(vc, "_items") : 0, *fs = vc ? mono_class_get_field_from_name(vc, "_size") : 0;
  if (!fi || !fs) { snprintf(g_padwhy, sizeof g_padwhy, "m_padInfo has no _items/_size"); return -1; }
  void *arr = 0; int n = 0; mono_field_get_value(v, fi, &arr); mono_field_get_value(v, fs, &n);
  if (!arr) return 0;
  int len = (int)mono_array_length(arr); if (n > len) n = len;
  if (n < 0 || n > 32) { snprintf(g_padwhy, sizeof g_padwhy, "m_padInfo says %d entries", n); return -1; }
  int all = 0, set = 0; char e[200] = ""; g_sys = 0;
  for (int i = 0; i < n; i++) { void *p = *(void **)mono_array_addr_with_size(arr, sizeof(void *), i); int b = 0; if (p) { mono_field_get_value(p, fButtons, &b); set++; if (fSysButtons) { unsigned long long sb = 0; mono_field_get_value(p, fSysButtons, &sb); g_sys |= sb; } }
    all |= b; if (b && strlen(e) < 150) { size_t l = strlen(e); snprintf(e + l, sizeof e - l, " #%d=0x%x", i, b); } }
  if (!g_paddesc) { g_paddesc = 1; lg("pad list: %d entries (%d filled), buttons held now:%s", n, set, e[0] ? e : " none"); }
  return all;
}
#define COMBO (0x1000 | 0x2000 | 0x4000 | 0x8000)   /* L2 + R2 + L3 + R3 */
static unsigned int g_lock = 0;   /* gc handle of our Locker, 0 = not locked */
static int g_susp = 0;
static void lock_on(const char *why) {
  if (!g_lock && cLocker && mLockerCtor) { void *o = mono_object_new(g_dom, cLocker); unsigned int lt = 0x3F, mask = 0x3F; void *a[2] = { &lt, &mask };
    if (o && !inv("PS button lock", mLockerCtor, o, a, 0)) { g_lock = mono_gchandle_new(o, 1); lg("PS + Share buttons LOCKED (%s)", why); } }
  if (!g_susp && mSuspend && !inv("pause profile picker", mSuspend, 0, 0, 0)) { g_susp = 1; lg("profile picker PAUSED (%s) - check says %d", why, suspended()); }
}
static void *g_lockobj(void) { extern void *mono_gchandle_get_target(unsigned int); return g_lock ? mono_gchandle_get_target(g_lock) : 0; }
static void lock_off(const char *why) {
  if (g_lock) { void *o = g_lockobj(); if (o) inv("PS button unlock", mDispose, o, 0, 0); mono_gchandle_free(g_lock); g_lock = 0; lg("PS + Share buttons UNLOCKED (%s)", why); }
  if (g_susp) { inv("bring back profile picker", mResume, 0, 0, 0); g_susp = 0; lg("profile picker BACK (%s) - check says %d", why, suspended()); }
}
static int combo_held(int *since) { int b = held_buttons(); if (b >= 0 && (b & COMBO) == COMBO) { if (!*since) *since = (int)time(0); return (int)time(0) - *since >= 1; } *since = 0; return 0; }
static int loading(void) {
  if (exists_(DIR_U "/memqn")) return 1;
  char st[32] = "", run[64] = ""; int fd = open(DIR_U "/clean/state", O_RDONLY); if (fd >= 0) { int n = read(fd, st, sizeof st - 1); if (n > 0) st[n] = 0; close(fd); }
  fd = open(DIR_U "/clean/run", O_RDONLY); if (fd >= 0) { int n = read(fd, run, sizeof run - 1); if (n > 0) run[n] = 0; close(fd); }
  return !strncmp(run, "prep", 4) && (!strncmp(st, "running", 7) || !strncmp(st, "paused", 6));
}
static int still_me(void) { char g[16] = ""; int fd = open(DIR_U "/shell.gen", O_RDONLY); if (fd >= 0) { int n = read(fd, g, sizeof g - 1); if (n > 0) g[n] = 0; close(fd); } return !strcmp(g, R2SH_NAME); }

/* 3.00: the PS menu's own Log Out for everyone (LogoutAll, LogoutFromPsImposeMenuV400 = 13) – controllers re-link and the PS4 shows
   the profile picker, so each player picks their (renamed) profile. Off only with /data/round2/shell-nologout. */
static void log_all_out(const char *why) {
  if (exists_(DIR_U "/shell-nologout")) { lg("log-out skipped (%s): shell-nologout is set", why); return; }
  if (!mLogoutAll) { lg("log-out skipped (%s): LogoutAll not found", why); return; }
  lg("buttons before log-out: 0x%x; picker paused now: %d", held_buttons(), suspended());
  int ty = 13; void *a1[1] = { &ty };
  if (!inv("log everyone out (PS menu's own Log Out)", mLogoutAll, 0, a1, 0)) lg("logged everyone out (%s) - the profile picker should show now", why);
  sleep(3); lg("3 s after log-out: picker paused %d", suspended());
}
/* 4.00: the 60-s self-test, only when asked (file shell-selftest, from the helper's "/_shtest"): picker paused + PS/Share locked,
   every change of the buttons logged (no need to time a press), L2+R2+L3+R3 held 1 s unlocks early, then everyone logged out. */
static void self_test(void) {
  unlink(DIR_U "/shell-selftest");
  lg("SELF-TEST: picker paused + PS button locked for 60 s - hold L2+R2+L3+R3 for 1 s to unlock early; every button change is logged");
  lock_on("self-test");
  int since = 0, last = -2; time_t t0 = time(0);
  while (time(0) - t0 < 60) {
    static unsigned long long lsys = ~0ULL;
    int b = held_buttons(); if (b != last || g_sys != lsys) { if (b < 0) lg("buttons: can't read - %s", g_padwhy); else lg("buttons: gen 0x%x sys 0x%llx%s", b, g_sys, (b & COMBO) == COMBO ? " (unlock combo held)" : ""); last = b; lsys = g_sys; }
    if (combo_held(&since)) { lg("unlock combo L2+R2+L3+R3 held 1 s"); break; } usleep(100000);
  }
  lock_off("self-test over");
  log_all_out("self-test");
}
static void *shell_thread(void *a) {
  (void)a; sleep(2); snprintf(g_log, sizeof g_log, DIR_U "/%s.txt", R2SH_NAME);
  put_(DIR_U "/shell.gen", R2SH_NAME); lg("round2_shell %s started inside the system menu", R2SH_VER);
  if (resolve()) { put_(DIR_U "/" R2SH_NAME ".txt.done", "1\n"); return 0; }
  { int b = held_buttons(); if (b < 0) lg("controllers: can't read the buttons - %s", g_padwhy); else lg("controllers: buttons now 0x%x (0 = none held / no controller)", b); }
  put_(DIR_U "/" R2SH_NAME ".txt.done", "1\n");
  if (exists_(DIR_U "/shell-selftest")) self_test();   /* loaded by "/_shtest" */
  /* WATCH */
  lg("WATCH: locks during member loads (max 10 min each), combo unlocks; self-test when asked (shell-selftest)");
  int was = 0, since = 0; time_t lt = 0, alive = 0;
  while (still_me()) {
    if (time(0) - alive >= 30) { char t[24]; snprintf(t, sizeof t, "%ld\n", (long)time(0)); put_(DIR_U "/shell.alive", t); alive = time(0); }
    int l = loading();
    if (!l && !was && exists_(DIR_U "/shell-selftest")) self_test();
    if (l && !was) { lg("member load started"); lt = time(0); lock_on("member load"); }
    if (l && g_lock && (time(0) - lt > 600)) lock_off("10 min limit");
    if (l && g_lock && combo_held(&since)) lock_off("unlock combo");
    if (!l && was) {   /* 7.00 (owner 8:22pm "continue blocking controllers? then after logout"): the PS button stays LOCKED until
                          after the log-out, so nobody can leave the tile for a game on the wrong profile; unlocked right after */
      lg("member load finished - PS button stays locked until everyone is logged out");
      char fg[40]; time_t t0 = time(0); rd_(DIR_U "/fg.txt", fg, sizeof fg); int tile = !strcmp(fg, "RTWO00001");
      while (!strcmp(fg, "RTWO00001") && time(0) - t0 < 20) { usleep(300000); rd_(DIR_U "/fg.txt", fg, sizeof fg); if (g_lock && combo_held(&since)) lock_off("unlock combo"); }   /* until the tile closes */
      lg("after the load: tile %s (%ld s)", !tile ? "wasn't open" : !strcmp(fg, "RTWO00001") ? "still open after 20 s" : "closed", (long)(time(0) - t0));
      sleep(2); rd_(DIR_U "/fg.txt", fg, sizeof fg);   /* the helper's pop-up first */
      if (fg[0] && strcmp(fg, "RTWO00001") && strcmp(fg, "CUSA00960")) lg("a game was opened before the log-out (%s) - it closes now (owner: just close)", fg);
      log_all_out("member load finished");
      lock_off("after the log-out - guests press PS and pick their profile"); }
    was = l; usleep(l ? 100000 : 1000000);
  }
  lock_off("a newer add-on took over"); lg("stopped - a newer add-on is running");
  return 0;
}
static int g_started = 0;
static void go(void) { if (g_started) return; g_started = 1; OrbisPthread th; scePthreadCreate(&th, NULL, (void *(*)(void *))shell_thread, NULL, "r2shell"); }
s32 attr_public plugin_load(s32 argc, const char *argv[]) { go(); return 0; }
s32 attr_public plugin_unload(s32 argc, const char *argv[]) { return 0; }
s32 attr_module_hidden module_start(s64 argc, const void *args) { go(); return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { return 0; }
