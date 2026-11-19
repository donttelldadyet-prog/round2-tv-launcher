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
attr_public u32 g_pluginVersion = 0x00000100;

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
static void *probe_thread(void *a) {
  (void)a; sleep(2);
  static const char *outs[] = { "/data/round2/shprobe.txt", "/user/data/round2/shprobe.txt", "/system_tmp/shprobe.txt", 0 }; const char *out = 0;
  for (int i = 0; outs[i] && g_fd < 0; i++) { g_fd = open(outs[i], O_WRONLY | O_CREAT | O_TRUNC, 0666); out = outs[i]; } if (g_fd < 0) return 0;
  wr("round2_shprobe 1.00 - read-only list of SceShellUI user/controller code\n");
  mono_thread_attach(mono_get_root_domain());
  const char *apps[] = { "/app0/psm/Application/app.exe", "/app0/psm/Application/platform.dll", 0 };
  for (int i = 0; apps[i]; i++) dump(apps[i]);
  const char *sb = sceKernelGetFsSandboxRandomWord(); char p[260];
  const char *libs[] = { "Sce.Vsh.ShellUI.Core.dll", "Sce.Vsh.UserService.dll", "Sce.Vsh.Orbis.UserService.dll", "Sce.PlayStation.Core.dll", "Sce.Vsh.SystemService.dll", "Sce.Vsh.Orbis.Pad.dll", 0 };
  for (int i = 0; sb && libs[i]; i++) { snprintf(p, sizeof p, "/%s/common/lib/%s", sb, libs[i]); dump(p); }
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
