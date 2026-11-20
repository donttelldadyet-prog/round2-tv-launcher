// round2_padprobe1 (owner 20 Nov 7:10pm, after round2_shell4 crashed Kaeden's menu on load): READ-ONLY probe.
// Why shell4 crashed: it was the first add-on to import 5 Mono functions never used inside the menu before (mono_class_get_rank,
// mono_class_get_element_class, mono_class_is_valuetype, mono_array_element_size, mono_field_get_offset) and it wrote no log line
// at all - so most likely one is not exported by the menu's Mono and loading the add-on itself failed. This probe imports ONLY
// functions already proven inside the menu (round2_sweep2 on all 6 PS4s, round2_shell3 on Kaeden), checks those 5 by name with
// sceKernelDlsym (missing = a log line, not a crash), and describes the controller list (m_padInfo) with names only.
// Calls nothing in the menu except PadManager.get_Instance (shell3 called it at 8:09am). Breadcrumb file per step.
#include "plugin_common.h"
#include "Common.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <orbis/libkernel.h>

#define R2_NAME "round2_padprobe1"
attr_public const char *g_pluginName = R2_NAME;
attr_public const char *g_pluginDesc = "Round2: read-only probe of the menu's controller list";
attr_public const char *g_pluginAuth = "Round2";
attr_public u32 g_pluginVersion = 0x00000100;

extern void *mono_get_root_domain(void);
extern void *mono_thread_attach(void *);
extern void *mono_assembly_get_image(void *);
extern void *mono_class_from_name(void *, const char *, const char *);
extern void *mono_class_get_method_from_name(void *, const char *, int);
extern void *mono_class_get_parent(void *);
extern void *mono_class_get_field_from_name(void *, const char *);
extern void mono_field_get_value(void *, void *, void *);
extern void *mono_runtime_invoke(void *, void *, void **, void **);
extern void *mono_object_get_class(void *);
extern uintptr_t mono_array_length(void *);
extern const char *mono_class_get_name(void *);
extern const char *mono_class_get_namespace(void *);
extern void *mono_class_get_fields(void *, void **);
extern const char *mono_field_get_name(void *);
extern void *mono_field_get_type(void *);
extern char *mono_type_get_name(void *);
extern int mono_field_get_flags(void *);
extern void mono_assembly_foreach(void (*)(void *, void *), void *);
extern void *mono_image_get_name(void *);

#define DIR_U "/user/data/round2"
static char g_log[96];
static void lg(const char *fmt, ...) {
  char b[700]; long t = ((long)time(0) + 8 * 3600) % 86400;
  int n = snprintf(b, sizeof b, "%02ld:%02ld:%02ld ", t / 3600, (t / 60) % 60, t % 60);
  va_list ap; va_start(ap, fmt); n += vsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap); if (n > (int)sizeof b - 2) n = sizeof b - 2; b[n++] = '\n';
  int fd = open(g_log, O_WRONLY | O_CREAT | O_APPEND, 0666); if (fd >= 0) { write(fd, b, n); close(fd); }
}
static void put_(const char *p, const char *s) { int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (fd >= 0) { write(fd, s, strlen(s)); close(fd); } }
static void step(const char *s) { put_(DIR_U "/" R2_NAME ".step", s); }

static void *g_img;
static void find_app(void *as, void *ud) { (void)ud; if (g_img) return; void *im = mono_assembly_get_image(as); const char *n = im ? (const char *)mono_image_get_name(im) : 0; if (n && !strcmp(n, "app")) g_img = im; }
static void fields_of(const char *label, void *cl) {   /* names + types + flags only */
  char f[600] = ""; void *it = 0, *fl; int k = 0;
  while (cl && (fl = mono_class_get_fields(cl, &it)) && k < 30) {
    size_t l = strlen(f); const char *fn = mono_field_get_name(fl); void *ty = mono_field_get_type(fl); char *tn = ty ? mono_type_get_name(ty) : 0;
    snprintf(f + l, sizeof f - l, "%s%s:%s%s", k++ ? ", " : "", fn ? fn : "?", tn ? tn : "?", (mono_field_get_flags(fl) & 0x10) ? "(static)" : "");
  }
  lg("%s fields: %s", label, f[0] ? f : "(none)");
}
static void parents(const char *label, void *cl) {
  char p[300] = ""; for (void *c = cl; c; c = mono_class_get_parent(c)) { size_t l = strlen(p); const char *ns = mono_class_get_namespace(c), *nm = mono_class_get_name(c); snprintf(p + l, sizeof p - l, "%s%s.%s", p[0] ? " < " : "", ns ? ns : "", nm ? nm : "?"); }
  lg("%s class chain: %s", label, p);
}
static void *probe_thread(void *a) {
  (void)a; sleep(2); snprintf(g_log, sizeof g_log, DIR_U "/%s.txt", R2_NAME);
  lg(R2_NAME " started (read-only)");
  step("1 dlsym check");
  { int mods[256]; size_t n = 0; int mono = -1;
    if (!sceKernelGetModuleList(mods, 256, &n)) for (size_t i = 0; i < n && i < 256; i++) { OrbisKernelModuleInfo mi; memset(&mi, 0, sizeof mi); mi.size = sizeof mi; if (!sceKernelGetModuleInfo(mods[i], &mi) && strstr(mi.name, "mono") && strstr(mi.name, "sgen")) { mono = mods[i]; lg("Mono module: %s (handle %d)", mi.name, mods[i]); } }
    const char *S[] = { "mono_class_get_rank", "mono_class_get_element_class", "mono_class_is_valuetype", "mono_array_element_size", "mono_field_get_offset", "mono_class_get_name", "mono_field_get_value", 0 };
    for (int i = 0; S[i]; i++) { void *p = 0; int r = mono >= 0 ? sceKernelDlsym(mono, S[i], &p) : -1; lg("export %s: %s (r=0x%x)", S[i], (!r && p) ? "FOUND" : "MISSING", (unsigned)r); }
  }
  step("2 runtime");
  void *dom = mono_get_root_domain(); if (!dom) { lg("no menu runtime"); goto done; }
  mono_thread_attach(dom); mono_assembly_foreach(find_app, 0); if (!g_img) { lg("app.exe not found"); goto done; }
  step("3 classes");
  void *pm = mono_class_from_name(g_img, "Sce.Vsh.ShellUI.PadInternal", "PadManager"), *pi = mono_class_from_name(g_img, "Sce.Vsh.ShellUI.PadInternal", "PadInfo");
  lg("PadManager %d, PadInfo %d", !!pm, !!pi);
  if (pi) { parents("PadInfo", pi); fields_of("PadInfo", pi); }
  if (!pm) goto done;
  fields_of("PadManager", pm);
  void *mInst = mono_class_get_method_from_name(pm, "get_Instance", 0), *fPad = mono_class_get_field_from_name(pm, "m_padInfo");
  if (!mInst || !fPad) { lg("get_Instance %d m_padInfo %d", !!mInst, !!fPad); goto done; }
  step("4 PadManager.Instance");
  lg("calling: PadManager.get_Instance");
  void *exc = 0, *inst = mono_runtime_invoke(mInst, 0, 0, &exc); if (exc || !inst) { lg("Instance: %s", exc ? "exception" : "null"); goto done; }
  step("5 m_padInfo");
  void *v = 0; mono_field_get_value(inst, fPad, &v); if (!v) { lg("m_padInfo is null"); goto done; }
  void *vc = mono_object_get_class(v); parents("m_padInfo", vc); fields_of("m_padInfo", vc);
  const char *vn = vc ? mono_class_get_name(vc) : 0;
  if (vn && strchr(vn, '[')) { lg("m_padInfo is an array, length %lu", (unsigned long)mono_array_length(v)); goto done; }
  void *fs = mono_class_get_field_from_name(vc, "_size"), *fc = mono_class_get_field_from_name(vc, "count"), *fi = mono_class_get_field_from_name(vc, "_items"), *fe = mono_class_get_field_from_name(vc, "entries");
  if (fs) { int n = -1; mono_field_get_value(v, fs, &n); lg("_size = %d", n); }
  if (fc) { int n = -1; mono_field_get_value(v, fc, &n); lg("count = %d", n); }
  if (fi) { void *ar = 0; mono_field_get_value(v, fi, &ar); if (ar) { void *ac = mono_object_get_class(ar); lg("_items: %s, length %lu", ac ? mono_class_get_name(ac) : "?", (unsigned long)mono_array_length(ar)); } }
  if (fe) { void *ar = 0; mono_field_get_value(v, fe, &ar); if (ar) { void *ac = mono_object_get_class(ar); lg("entries: %s, length %lu", ac ? mono_class_get_name(ac) : "?", (unsigned long)mono_array_length(ar)); } }
done:
  step("done");
  lg("finished");
  put_(DIR_U "/" R2_NAME ".txt.done", "1\n");
  return 0;
}
static int g_started;
static void go(void) { if (g_started) return; g_started = 1; OrbisPthread th; scePthreadCreate(&th, NULL, (void *(*)(void *))probe_thread, NULL, "r2padprobe"); }
s32 attr_public plugin_load(s32 argc, const char *argv[]) { (void)argc; (void)argv; go(); return 0; }
s32 attr_public plugin_unload(s32 argc, const char *argv[]) { (void)argc; (void)argv; return 0; }
s32 attr_module_hidden module_start(s64 argc, const void *args) { (void)argc; (void)args; go(); return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { (void)argc; (void)args; return 0; }
