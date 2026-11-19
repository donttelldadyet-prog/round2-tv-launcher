// round2_shell – Round2 add-on inside the PS4's system menu (SceShellUI). Owner, 20 Nov 2026 3:34–5:06am:
//   "once saves are loaded … logout, so that players can reselect their profiles seamlessly"; "a controller lock until after the
//   profiles are set: ps button is locked … unlock button combo like L3 L2 R3 R2"; worry: "turning on a controller would call the
//   profile picker screen which would clash".
// Uses ONLY the menu's own functions, found by round2_shprobe 1.00–1.03 (read-only):
//   Sce.Vsh.ShellUI.LoginMgrUtil.SuspendLoginScreen() / ResumeLoginScreen() / IsSuspendedLoginScreen()  – the profile picker
//   Sce.Vsh.ShellUI.PadInternal.Locker(LockType lock, LockType uiMask) + DisposableObj.Dispose()          – PS/Share button lock
//   Sce.Vsh.ShellUI.LoginMgrUtil.LogoutAll(LogoutMethodType)  (13 = LogoutFromPsImposeMenuV400, the PS menu's own Log Out)
//   PadManager.Instance.m_padInfo[i].m_curGeneralButtons (GamePadButtons: L2 0x1000, R2 0x2000, L3 0x4000, R3 0x8000)
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

#define R2SH_VER "1.00"
#define R2SH_NAME "round2_shell2"
attr_public const char *g_pluginName = R2SH_NAME;
attr_public const char *g_pluginDesc = "Round2: profile picker pause, PS button lock, proper log-out";
attr_public const char *g_pluginAuth = "Round2";
attr_public u32 g_pluginVersion = 0x00000200;

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
/* buttons held on any connected controller (OR of all), -1 if they can't be read */
static int held_buttons(void) {
  if (!mPadInst || !fPadInfo || !fButtons) return -1; void *pmi = 0; g_quiet_calls = 1; int bad = inv("pads", mPadInst, 0, 0, &pmi); g_quiet_calls = 0; if (bad || !pmi) return -1;
  void *list = 0; mono_field_get_value(pmi, fPadInfo, &list); if (!list) return -1;
  void *lc = mono_object_get_class(list); void *fi = mono_class_get_field_from_name(lc, "_items"), *fs = mono_class_get_field_from_name(lc, "_size"); if (!fi || !fs) return -1;
  void *arr = 0; int size = 0; mono_field_get_value(list, fi, &arr); mono_field_get_value(list, fs, &size); if (!arr || size < 0 || size > 8) return -1;
  int all = 0; for (int i = 0; i < size && i < (int)mono_array_length(arr); i++) { void *p = *(void **)mono_array_addr_with_size(arr, sizeof(void *), i); int b = 0; if (p) { mono_field_get_value(p, fButtons, &b); all |= b; } }
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

static void *shell_thread(void *a) {
  (void)a; sleep(2); snprintf(g_log, sizeof g_log, DIR_U "/%s.txt", R2SH_NAME);
  put_(DIR_U "/shell.gen", R2SH_NAME); lg("round2_shell %s started inside the system menu", R2SH_VER);
  if (resolve()) { put_(DIR_U "/" R2SH_NAME ".txt.done", "1\n"); return 0; }
  lg("controllers: buttons now 0x%x (-1 = can't read)", held_buttons());
  /* SELF-TEST */
  lg("SELF-TEST: picker paused + PS button locked for 60 s - hold L2+R2+L3+R3 for 1 s to unlock early");
  lock_on("self-test"); put_(DIR_U "/" R2SH_NAME ".txt.done", "1\n");
  int since = 0; time_t t0 = time(0);
  while (time(0) - t0 < 60) { if (combo_held(&since)) { lg("unlock combo L2+R2+L3+R3 seen"); break; } usleep(100000); }
  lock_off("self-test over");
  /* WATCH */
  lg("WATCH: locks during member loads (max 10 min each), combo unlocks");
  int was = 0, tries = 0; time_t lt = 0; int combo_out = 0;
  while (still_me()) {
    int l = loading();
    if (l && !was) { lg("member load started"); combo_out = 0; lt = time(0); lock_on("member load"); }
    if (l && g_lock && (time(0) - lt > 600)) { lock_off("10 min limit"); combo_out = 1; }
    if (l && g_lock && combo_held(&since)) { lock_off("unlock combo"); combo_out = 1; }
    if (!l && was) { lock_off("member load finished"); if (exists_(DIR_U "/shell-logout") && mLogoutAll) { int ty = 13; void *a1[1] = { &ty }; sleep(2); if (!inv("log everyone out", mLogoutAll, 0, a1, 0)) lg("logged everyone out (PS menu's own Log Out)"); } }
    was = l; (void)combo_out; (void)tries; usleep(l ? 100000 : 1000000);
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
