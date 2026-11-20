// round2_shell – Round2 add-on inside the PS4's system menu (SceShellUI). Owner, 20 Nov 2026 3:34–5:06am:
//   "once saves are loaded … logout, so that players can reselect their profiles seamlessly"; "a controller lock until after the
//   profiles are set: ps button is locked … unlock button combo like L3 L2 R3 R2"; worry: "turning on a controller would call the
//   profile picker screen which would clash".
// Uses ONLY the menu's own functions, found by round2_shprobe 1.00–1.03 (read-only):
//   Sce.Vsh.ShellUI.LoginMgrUtil.SuspendLoginScreen() / ResumeLoginScreen() / IsSuspendedLoginScreen()  – the profile picker
//   Sce.Vsh.ShellUI.PadInternal.Locker(LockType lock, LockType uiMask) + DisposableObj.Dispose()          – PS/Share button lock
//   Sce.Vsh.ShellUI.LoginMgrUtil.LogoutAll(LogoutMethodType)  (13 = LogoutFromPsImposeMenuV400, the PS menu's own Log Out)
//   PadManager.Instance.m_padInfo[i].m_curGeneralButtons (GamePadButtons: L2 0x1000, R2 0x2000, L3 0x4000, R3 0x8000)
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

#define R2SH_VER "4.00"
#define R2SH_NAME "round2_shell4"
attr_public const char *g_pluginName = R2SH_NAME;
attr_public const char *g_pluginDesc = "Round2: profile picker pause, PS button lock, proper log-out";
attr_public const char *g_pluginAuth = "Round2";
attr_public u32 g_pluginVersion = 0x00000400;

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
extern int mono_class_get_rank(void *);
extern void *mono_class_get_element_class(void *);
extern int mono_class_is_valuetype(void *);
extern int mono_array_element_size(void *);
extern unsigned int mono_field_get_offset(void *);
extern void *mono_class_get_fields(void *, void **);
extern const char *mono_field_get_name(void *);

#define DIR_U "/user/data/round2"
static char g_log[96];
static void lg(const char *fmt, ...) {
  char b[400]; long t = ((long)time(0) + 8 * 3600) % 86400;
  int n = snprintf(b, sizeof b, "%02ld:%02ld:%02ld ", t / 3600, (t / 60) % 60, t % 60);
  va_list ap; va_start(ap, fmt); n += vsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap); if (n > (int)sizeof b - 2) n = sizeof b - 2; b[n++] = '\n';
  int fd = open(g_log, O_WRONLY | O_CREAT | O_APPEND, 0666); if (fd >= 0) { write(fd, b, n); close(fd); }
}
static int exists_(const char *p) { struct stat st; return !stat(p, &st); }
static void put_(const char *p, const char *s) { int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (fd >= 0) { write(fd, s, strlen(s)); close(fd); } }

static void *g_dom, *g_img, *mSuspend, *mResume, *mIsSusp, *mLogoutAll, *cLocker, *mLockerCtor, *mDispose, *mPadInst, *fPadInfo, *fButtons;
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
  void *pi = mono_class_from_name(g_img, "Sce.Vsh.ShellUI.PadInternal", "PadInfo"); if (pi) fButtons = mono_class_get_field_from_name(pi, "m_curGeneralButtons");
  lg("found: suspend %d resume %d is-suspended %d logout-all %d locker %d/%d/%d pads %d/%d/%d", !!mSuspend, !!mResume, !!mIsSusp, !!mLogoutAll, !!cLocker, !!mLockerCtor, !!mDispose, !!mPadInst, !!fPadInfo, !!fButtons);
  return 0;
}
extern void *mono_object_unbox(void *);
static int suspended(void) { void *r = 0; if (inv("is-suspended", mIsSusp, 0, 0, &r) || !r) return -1; return *(unsigned char *)mono_object_unbox(r) ? 1 : 0; }
/* 4.00: buttons held on any connected controller (OR of all). 0 = none held or no controller; -1 = can't read (why -> g_padwhy).
   m_padInfo may be an array or a List<>, of objects or of structs - handled. */
static char g_padwhy[200];
static int g_paddesc = 0;
static int pad_at(void *arr, int i, int vt, int esz, unsigned int off) {   /* buttons of element i */
  int b = 0;
  if (vt) { if (esz <= 0 || off < 16) return 0; char *e = mono_array_addr_with_size(arr, esz, i); if (e) memcpy(&b, e + (off - 16), sizeof b); }   /* struct: field offsets count the 16-byte object header */
  else { void *p = *(void **)mono_array_addr_with_size(arr, sizeof(void *), i); if (p) mono_field_get_value(p, fButtons, &b); }
  return b;
}
static int held_buttons(void) {
  if (!mPadInst || !fPadInfo || !fButtons) { snprintf(g_padwhy, sizeof g_padwhy, "pad functions not found"); return -1; }
  void *pmi = 0; g_quiet_calls = 1; int bad = inv("pads", mPadInst, 0, 0, &pmi); g_quiet_calls = 0;
  if (bad || !pmi) { snprintf(g_padwhy, sizeof g_padwhy, "PadManager.Instance gave nothing"); return -1; }
  void *v = 0; mono_field_get_value(pmi, fPadInfo, &v); if (!v) { snprintf(g_padwhy, sizeof g_padwhy, "m_padInfo is empty (null)"); return -1; }
  void *vc = mono_object_get_class(v); const char *vn = vc ? mono_class_get_name(vc) : "?";
  void *arr = 0, *ac = 0; int n = 0;
  if (vc && mono_class_get_rank(vc) > 0) { arr = v; ac = vc; n = (int)mono_array_length(arr); }
  else { void *fi = vc ? mono_class_get_field_from_name(vc, "_items") : 0, *fs = vc ? mono_class_get_field_from_name(vc, "_size") : 0;
    if (!fi || !fs) { snprintf(g_padwhy, sizeof g_padwhy, "m_padInfo is a %.60s - not an array or list", vn ? vn : "?"); return -1; }
    mono_field_get_value(v, fi, &arr); mono_field_get_value(v, fs, &n); if (arr) { ac = mono_object_get_class(arr); if (n > (int)mono_array_length(arr)) n = (int)mono_array_length(arr); } }
  if (!arr || !ac) return 0;   /* an empty list = no controller */
  if (n < 0 || n > 16) { snprintf(g_padwhy, sizeof g_padwhy, "m_padInfo (%.40s) says %d entries", vn ? vn : "?", n); return -1; }
  void *ec = mono_class_get_element_class(ac); int vt = ec ? mono_class_is_valuetype(ec) : 0, esz = mono_array_element_size(ac); unsigned int off = mono_field_get_offset(fButtons);
  int all = 0, each[16] = { 0 };
  for (int i = 0; i < n; i++) { each[i] = pad_at(arr, i, vt, esz, off); all |= each[i]; }
  if (!g_paddesc) {   /* once per menu start: what the pad list really is (owner 20 Nov: log everything) */
    g_paddesc = 1; char e[120] = ""; for (int i = 0; i < n && i < 8; i++) { size_t l = strlen(e); snprintf(e + l, sizeof e - l, "%s0x%x", i ? " " : "", each[i]); }
    lg("pad list: %s, %s of %s (%s, %d bytes each), %d entries, buttons field at %u: %s", vn ? vn : "?", mono_class_get_rank(vc) > 0 ? "array" : "list",
       ec ? mono_class_get_name(ec) : "?", vt ? "struct" : "object", esz, n, off, e[0] ? e : "-");
    if (ec) { char f[300] = ""; void *it = 0, *fl; int k = 0; while ((fl = mono_class_get_fields(ec, &it)) && k < 24) { size_t l = strlen(f); const char *fn = mono_field_get_name(fl); snprintf(f + l, sizeof f - l, "%s%s@%u", k++ ? " " : "", fn ? fn : "?", mono_field_get_offset(fl)); }
      lg("PadInfo fields: %s", f); }
  }
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
    int b = held_buttons(); if (b != last) { if (b < 0) lg("buttons: can't read - %s", g_padwhy); else lg("buttons: 0x%x%s", b, (b & COMBO) == COMBO ? " (unlock combo held)" : ""); last = b; }
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
    if (!l && was) { lock_off("member load finished"); sleep(8); /* the tile shows "Ready, <name>!" */ log_all_out("member load finished"); }
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
