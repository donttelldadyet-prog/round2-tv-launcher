// round2_shprobe – Round2 READ-ONLY probe of the PS4 system menu (SceShellUI), owner 20 Nov 2026 3:50am:
// "probe the sceShellui and make logs using the helper". Loaded into SceShellUI by the Round2 helper (GoldHEN
// sys_sdk_proc_prx_load). It only LISTS names: every class + method of the menu's C# program (app.exe, platform.dll and the
// other Sce.Vsh assemblies) whose class or method name mentions log-in/out, users, controllers/pads, the PS button, the
// quick menu or the profile picker. Nothing is called, patched or changed. Output: /data/round2/shprobe.txt (+ .done).
// Runs once in its own thread, then returns; the module stays loaded doing nothing.
#include "plugin_common.h"
#include "Common.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

attr_public const char *g_pluginName = "round2_shprobe";
attr_public const char *g_pluginDesc = "Round2: read-only list of the system menu's user/controller functions";
attr_public const char *g_pluginAuth = "Round2";
attr_public u32 g_pluginVersion = 0x00000103;   /* 1.03: every loaded assembly; enum values. 1.02:   /* 1.02: output where the menu can write (/user/data). 1.01 (20 Nov 5am): full signatures of the classes found by 1.00 */

extern void *mono_get_root_domain(void);
extern void *mono_thread_attach(void *);
extern void *mono_domain_assembly_open(void *, const char *);
extern void *mono_assembly_get_image(void *);
extern int mono_image_get_table_rows(void *, int);
extern void *mono_class_get(void *, unsigned int);
extern const char *mono_class_get_name(void *);
extern const char *mono_class_get_namespace(void *);
extern void *mono_class_get_methods(void *, void **);
extern const char *mono_method_get_name(void *);
extern void *mono_method_signature(void *);
extern unsigned int mono_signature_get_param_count(void *);
extern const char *sceKernelGetFsSandboxRandomWord(void);
extern char *mono_method_full_name(void *, int);
extern unsigned int mono_method_get_flags(void *, unsigned int *);
extern void *mono_class_get_nesting_type(void *);
extern void *mono_class_get_parent(void *);
extern void *mono_class_get_fields(void *, void **);
extern const char *mono_field_get_name(void *);
extern void *mono_field_get_type(void *);
extern unsigned int mono_field_get_flags(void *);
extern char *mono_type_get_name(void *);
extern void mono_assembly_foreach(void (*)(void *, void *), void *);
extern void *mono_class_vtable(void *, void *);
extern void mono_field_static_get_value(void *, void *, void *);
extern int mono_class_is_enum(void *);
extern void *mono_image_get_name(void *);

#define MAXOUT (900 * 1024)
static int g_fd = -1, g_len = 0;
static void wr(const char *s) { int n = (int)strlen(s); if (g_fd < 0 || g_len + n > MAXOUT) return; write(g_fd, s, n); g_len += n; }

static const char *KEYS[] = { "logout", "logoff", "login", "logon", "signin", "signout", "loginuser", "usermgr", "usermanager",
  "userselect", "selectuser", "switchuser", "pad", "controller", "psbutton", "homebutton", "quickmenu", "powermenu", "standby", 0 };
static int hit(const char *s) {
  char l[160]; int i = 0; for (; s && s[i] && i < 159; i++) l[i] = (char)tolower((unsigned char)s[i]); l[i] = 0;
  for (int k = 0; KEYS[k]; k++) if (strstr(l, KEYS[k])) return 1; return 0;
}
static void dump(const char *path) {
  void *dom = mono_get_root_domain(); if (!dom) { wr("no root domain\n"); return; }
  void *as = mono_domain_assembly_open(dom, path); char b[400];
  if (!as) { snprintf(b, sizeof b, "## %s: not open\n", path); wr(b); return; }
  void *img = mono_assembly_get_image(as); if (!img) { snprintf(b, sizeof b, "## %s: no image\n", path); wr(b); return; }
  int rows = mono_image_get_table_rows(img, 2 /* MONO_TABLE_TYPEDEF */), nc = 0, nm = 0;
  snprintf(b, sizeof b, "## %s: %d types\n", path, rows); wr(b);
  for (int i = 1; i < rows && g_len < MAXOUT; i++) {
    void *k = mono_class_get(img, 0x02000000u | (unsigned)(i + 1)); if (!k) continue;
    const char *cn = mono_class_get_name(k), *ns = mono_class_get_namespace(k); int ch = hit(cn), shown = 0;
    void *it = 0, *m;
    while ((m = mono_class_get_methods(k, &it))) {
      const char *mn = mono_method_get_name(m); if (!mn) continue;
      if (ch || hit(mn)) {
        if (!shown) { snprintf(b, sizeof b, "%s.%s\n", ns ? ns : "", cn ? cn : "?"); wr(b); shown = 1; nc++; }
        void *sg = mono_method_signature(m); snprintf(b, sizeof b, "  %s(%u)\n", mn, sg ? mono_signature_get_param_count(sg) : 99u); wr(b); nm++;
      }
    }
  }
  snprintf(b, sizeof b, "## %s: %d classes, %d methods listed\n", path, nc, nm); wr(b);
}
static const char *TARGETS[] = { "LoginMgrUtil", "LoginMgrUtilInternal", "EventAppExternal", "TeamChatExternal", "PadManager", "PadInfo",
  "PageBase", "LogoutJob", "PSButtonLocker", "PSButtonLongLocker", "PSButtonDoubleLocker", "PSButtonLockAlertSuppressor", "PSButtonListener",
  "QuickMenuSelector", "LoginSeaquence", 0 };
static const char *ENUMS[] = { "LogoutMethodType", "LockType", "DisconnectDeviceType", "ButtonType", "GamePadButtons", "SystemButtons", "Mode", "IdType", 0 };
static const char *MORE[] = { "Locker", "MenuSuppressor", "DisposableObj", "LoginMgrProxy", "NativeCall", 0 };
static int in_list(const char **L, const char *n) { for (int i = 0; n && L[i]; i++) if (!strcmp(n, L[i])) return 1; return 0; }
static int is_target(const char *n) { for (int i = 0; n && TARGETS[i]; i++) if (!strcmp(n, TARGETS[i])) return 1; return 0; }
static void sigs_img(void *img) {   /* 1.01: every method (static or not, parameter types) + fields of the target classes */
  char b[700];
  int rows = mono_image_get_table_rows(img, 2);
  for (int i = 1; i < rows && g_len < MAXOUT; i++) {
    void *k = mono_class_get(img, 0x02000000u | (unsigned)(i + 1)); if (!k) continue;
    const char *cn = mono_class_get_name(k); int en = in_list(ENUMS, cn) && mono_class_is_enum(k), more = in_list(MORE, cn);
    if (en) { void *vt = mono_class_vtable(mono_get_root_domain(), k); void *nest = mono_class_get_nesting_type(k);
      snprintf(b, sizeof b, "\n### ENUM %s.%s%s%s\n", mono_class_get_namespace(k), cn, nest ? "  nested in " : "", nest ? mono_class_get_name(nest) : ""); wr(b);
      void *it2 = 0, *f2; while ((f2 = mono_class_get_fields(k, &it2))) { unsigned fl2 = mono_field_get_flags(f2); if (!(fl2 & 0x10)) continue; long long val = 0; if (vt) mono_field_static_get_value(vt, f2, &val);
        snprintf(b, sizeof b, "  %s = %lld (0x%llx)\n", mono_field_get_name(f2), val, (unsigned long long)val); wr(b); }
      continue; }
    if (!is_target(cn) && !more) continue;
    void *nest = mono_class_get_nesting_type(k), *par = mono_class_get_parent(k);
    snprintf(b, sizeof b, "\n### CLASS %s.%s%s%s  (parent %s)\n", mono_class_get_namespace(k), cn, nest ? "  nested in " : "", nest ? mono_class_get_name(nest) : "", par ? mono_class_get_name(par) : "-"); wr(b);
    void *it = 0, *f; while ((f = mono_class_get_fields(k, &it))) { void *ft = mono_field_get_type(f); char *tn = ft ? mono_type_get_name(ft) : 0;
      snprintf(b, sizeof b, "  field %s%s %s\n", (mono_field_get_flags(f) & 0x10) ? "static " : "", tn ? tn : "?", mono_field_get_name(f)); wr(b); }
    it = 0; void *m; while ((m = mono_class_get_methods(k, &it))) { unsigned int ifl = 0, fl = mono_method_get_flags(m, &ifl); char *fn = mono_method_full_name(m, 1);
      snprintf(b, sizeof b, "  %s%s\n", (fl & 0x10) ? "static " : "", fn ? fn : mono_method_get_name(m)); wr(b); }
  }
}
static void each_asm(void *as, void *ud) { (void)ud; void *img = mono_assembly_get_image(as); if (!img) return; const char *n = (const char *)mono_image_get_name(img); char b[200]; snprintf(b, sizeof b, "\n## ASSEMBLY %s\n", n ? n : "?"); wr(b); sigs_img(img); }
static void *probe_thread(void *a) {
  (void)a; sleep(2);
  static const char *outs[] = { "/user/data/round2/round2_shprobe3.txt", "/data/round2/round2_shprobe3.txt", "/user/data/GoldHEN/shprobe.txt", "/data/GoldHEN/shprobe.txt", "/system_tmp/shprobe.txt", 0 }; const char *out = 0;
  for (int i = 0; outs[i] && g_fd < 0; i++) { g_fd = open(outs[i], O_WRONLY | O_CREAT | O_TRUNC, 0666); out = outs[i]; } if (g_fd < 0) return 0;
  wr("round2_shprobe 1.01 - read-only signatures of SceShellUI user/controller/PS-button classes\n");
  mono_thread_attach(mono_get_root_domain());
  mono_assembly_foreach(each_asm, 0);
  const char *sb = sceKernelGetFsSandboxRandomWord(); char p[260];
  const char *libs[] = { "Sce.Vsh.ShellUI.Core.dll", "Sce.Vsh.UserService.dll", "Sce.Vsh.Orbis.UserService.dll", "Sce.PlayStation.Core.dll", "Sce.Vsh.SystemService.dll", "Sce.Vsh.Orbis.Pad.dll", 0 };
  (void)sb; (void)p; (void)libs;
  wr("## end\n"); close(g_fd); g_fd = -1;
  char dn[80]; snprintf(dn, sizeof dn, "%s.done", out); int d = open(dn, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (d >= 0) { write(d, "1\n", 2); close(d); }
  return 0;
}
static int g_started = 0;
static void go(void) { if (g_started) return; g_started = 1; OrbisPthread th; scePthreadCreate(&th, NULL, (void *(*)(void *))probe_thread, NULL, "r2shprobe"); }
s32 attr_public plugin_load(s32 argc, const char *argv[]) { go(); return 0; }
s32 attr_public plugin_unload(s32 argc, const char *argv[]) { return 0; }
s32 attr_module_hidden module_start(s64 argc, const void *args) { go(); return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { return 0; }
