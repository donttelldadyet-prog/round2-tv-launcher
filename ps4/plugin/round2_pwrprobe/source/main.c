// round2_pwrprobe – Round2 READ-ONLY probe of the PS4 system menu (SceShellUI) for rest mode, waking and the network in rest.
// Owner 20 Nov 2026 5:35am: "probe for the network wake and sleep function so that we don't have to use remote wake anymore,
// we need to find out if it works differently for 13.50, 9.00 and 11.00 to find the discrepancy". Same probe on every
// firmware → the text files can be compared line by line.
// It only LISTS names and fixed numbers that are written into the menu's program (literal constants, enum values, the names of
// the PS4 system functions the menu calls). Nothing is called, patched or changed; no class is started (constants are read
// from the metadata tables). Output: /user/data/round2/<name>.txt (+ .done) – the helper uploads it.
#include "plugin_common.h"
#include "Common.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define R2NAME "round2_pwrprobe1"
attr_public const char *g_pluginName = R2NAME;
attr_public const char *g_pluginDesc = "Round2: read-only list of the system menu's rest-mode / wake / network-in-rest functions";
attr_public const char *g_pluginAuth = "Round2";
attr_public u32 g_pluginVersion = 0x00000100;

extern void *mono_get_root_domain(void);
extern void *mono_thread_attach(void *);
extern void *mono_assembly_get_image(void *);
extern int mono_image_get_table_rows(void *, int);
extern void *mono_image_get_table_info(void *, int);
extern int mono_metadata_decode_row(void *, int, unsigned int *, int);
extern const char *mono_metadata_string_heap(void *, unsigned int);
extern void *mono_class_get(void *, unsigned int);
extern const char *mono_class_get_name(void *);
extern const char *mono_class_get_namespace(void *);
extern void *mono_class_get_methods(void *, void **);
extern const char *mono_method_get_name(void *);
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
extern int mono_class_is_enum(void *);
extern void *mono_image_get_name(void *);

#define MAXOUT (1800 * 1024)
static int g_fd = -1, g_len = 0;
static void wr(const char *s) { int n = (int)strlen(s); if (g_fd < 0 || g_len + n > MAXOUT) return; write(g_fd, s, n); g_len += n; }

/* words that mark rest mode, waking, power, the network in rest, timers/alarms, Remote Play (it wakes the PS4 too) */
static const char *KEYS[] = { "standby", "restmode", "rest_mode", "inrest", "wakeup", "wake_", "wakeon", "towake", "waking",
  "power", "systemstate", "shutdown", "suspend", "resume", "remoteplay", "remote_play", "keepconn", "stayconn", "netconnect",
  "networkconnect", "connectinrest", "internetin", "autodownload", "autoupdate", "bgdl", "icc", "alarm", "rtc", "schedul",
  "usbsupply", "supplypower", "turnon", "turn_on", "turnoff", "bootfrom", "ddp", "wol", "magic", "sleep", "idle", "dimmer",
  "autooff", "auto_off", "timer", "rpwake", 0 };
static int hit(const char *s) {
  char l[200]; int i = 0; for (; s && s[i] && i < 199; i++) l[i] = (char)tolower((unsigned char)s[i]); l[i] = 0;
  for (int k = 0; KEYS[k]; k++) if (strstr(l, KEYS[k])) return 1; return 0;
}
static int n_cls, n_meth, n_enum, n_pinv, n_reg;
static int is_reg_img(const char *n) { return n && (strstr(n, "Registry") || strstr(n, "registry")); }

/* constants (const fields, enum members) straight from the program's metadata tables ("Constant" table 0x0B) – pure reading of
   the file in memory, no class is started. owner class from the TypeDef FieldList column. */
extern const char *mono_metadata_blob_heap(void *, unsigned int);
extern unsigned int mono_metadata_decode_blob_size(const char *, const char **);
#include <stdlib.h>
static void consts(void *img, const char *iname) {
  char b[700]; void *ct = mono_image_get_table_info(img, 0x0B), *ft = mono_image_get_table_info(img, 0x04), *td = mono_image_get_table_info(img, 0x02);
  int nc = mono_image_get_table_rows(img, 0x0B), nt = mono_image_get_table_rows(img, 0x02), reg = is_reg_img(iname), shown = 0;
  if (!ct || !ft || !td || nc <= 0 || nt <= 0) return;
  unsigned int *fl = (unsigned int *)malloc(sizeof(unsigned int) * (size_t)nt); if (!fl) return;
  for (int i = 0; i < nt; i++) { unsigned int r[6] = { 0 }; mono_metadata_decode_row(td, i, r, 6); fl[i] = r[4]; }
  int last = -1;
  for (int i = 0; i < nc && g_len < MAXOUT; i++) {
    unsigned int c[3] = { 0 }; mono_metadata_decode_row(ct, i, c, 3); if ((c[1] & 3) != 0) continue;   /* only fields */
    unsigned int frow = c[1] >> 2, f[3] = { 0 }; if (!frow) continue; mono_metadata_decode_row(ft, (int)frow - 1, f, 3);
    const char *fnm = mono_metadata_string_heap(img, f[1]);
    int lo = 0, hi = nt - 1, own = 0; while (lo <= hi) { int mid = (lo + hi) / 2; if (fl[mid] <= frow) { own = mid; lo = mid + 1; } else hi = mid - 1; }
    unsigned int t[6] = { 0 }; mono_metadata_decode_row(td, own, t, 6);
    const char *cn = mono_metadata_string_heap(img, t[1]), *ns = mono_metadata_string_heap(img, t[2]);
    if (!(reg || hit(fnm) || hit(cn) || hit(ns))) continue;
    if (!shown) { wr("\n# CONSTANTS (name = value)\n"); shown = 1; }
    if (own != last) { snprintf(b, sizeof b, "### %s.%s\n", ns, cn); wr(b); last = own; }
    const char *p = mono_metadata_blob_heap(img, c[2]), *q = p; unsigned int len = p ? mono_metadata_decode_blob_size(p, &q) : 0;
    unsigned long long v = 0; int ty = (int)(c[0] & 0xff);
    if (ty == 0x0e) { char u[200]; int k = 0; for (unsigned int j = 0; j + 1 < len && k < 199; j += 2) u[k++] = q[j] ? q[j] : '?'; u[k] = 0;
      snprintf(b, sizeof b, "  %s = \"%s\"\n", fnm, u); wr(b); n_reg += reg; continue; }
    for (unsigned int j = 0; j < len && j < 8; j++) v |= (unsigned long long)(unsigned char)q[j] << (8 * j);
    long long sv = (long long)v; if (len == 4 && (ty == 0x08)) sv = (int)(unsigned int)v; if (len == 2 && ty == 0x06) sv = (short)v; if (len == 1 && ty == 0x04) sv = (signed char)v;
    snprintf(b, sizeof b, "  %s = %lld (0x%llx)\n", fnm, sv, v); wr(b); n_reg += reg;
  }
  free(fl);
}
static void full_class(void *k) {
  char b[800]; const char *cn = mono_class_get_name(k); void *nest = mono_class_get_nesting_type(k), *par = mono_class_get_parent(k);
  snprintf(b, sizeof b, "\n### CLASS %s.%s%s%s  (parent %s)\n", mono_class_get_namespace(k), cn, nest ? "  nested in " : "", nest ? mono_class_get_name(nest) : "", par ? mono_class_get_name(par) : "-"); wr(b);
  void *it = 0, *f; while ((f = mono_class_get_fields(k, &it))) { unsigned fl = mono_field_get_flags(f);
    if (fl & 0x40) continue;   /* constants: see CONSTANTS */
    void *ft = mono_field_get_type(f); char *tn = ft ? mono_type_get_name(ft) : 0;
    snprintf(b, sizeof b, "  field %s%s %s\n", (fl & 0x10) ? "static " : "", tn ? tn : "?", mono_field_get_name(f)); wr(b); }
  it = 0; void *m; while ((m = mono_class_get_methods(k, &it))) { unsigned int ifl = 0, fl = mono_method_get_flags(m, &ifl); char *fn = mono_method_full_name(m, 1);
    snprintf(b, sizeof b, "  %s%s%s\n", (fl & 0x10) ? "static " : "", (fl & 0x2000) ? "native " : "", fn ? fn : mono_method_get_name(m)); wr(b); }
}
static void types(void *img, const char *iname) {
  char b[800]; int rows = mono_image_get_table_rows(img, 2); (void)iname;
  for (int i = 1; i < rows && g_len < MAXOUT; i++) {
    void *k = mono_class_get(img, 0x02000000u | (unsigned)(i + 1)); if (!k) continue;
    const char *cn = mono_class_get_name(k), *ns = mono_class_get_namespace(k);
    int ch = hit(cn) || hit(ns);
    if (mono_class_is_enum(k)) { if (ch) { snprintf(b, sizeof b, "\n### ENUM %s.%s (values under CONSTANTS)\n", ns, cn); wr(b); n_enum++; } continue; }
    if (ch) { full_class(k); n_cls++; continue; }
    int sh = 0; void *it = 0, *m;
    while ((m = mono_class_get_methods(k, &it))) { const char *mn = mono_method_get_name(m); if (!mn || !hit(mn)) continue;
      if (!sh) { snprintf(b, sizeof b, "\n### IN %s.%s\n", ns, cn); wr(b); sh = 1; }
      unsigned int ifl = 0, fl = mono_method_get_flags(m, &ifl); char *fn = mono_method_full_name(m, 1);
      snprintf(b, sizeof b, "  %s%s%s\n", (fl & 0x10) ? "static " : "", (fl & 0x2000) ? "native " : "", fn ? fn : mn); wr(b); n_meth++; }
  }
}
/* every PS4 system function the menu's program calls (P/Invoke "ImplMap" table): module, function name, C# method. All of them
   (not only matching ones) so two firmwares can be compared; matching ones are marked "*". */
static void pinvokes(void *img) {
  char b[600]; void *im = mono_image_get_table_info(img, 0x1C), *mr = mono_image_get_table_info(img, 0x1A), *md = mono_image_get_table_info(img, 0x06);
  int n = mono_image_get_table_rows(img, 0x1C); if (!im || n <= 0) return;
  for (int i = 0; i < n && g_len < MAXOUT; i++) {
    unsigned int c[4] = { 0 }, m[1] = { 0 }, d[6] = { 0 };
    mono_metadata_decode_row(im, i, c, 4);
    const char *fn = mono_metadata_string_heap(img, c[2]);
    if (mr && c[3]) mono_metadata_decode_row(mr, (int)c[3] - 1, m, 1);
    const char *mod = (mr && c[3]) ? mono_metadata_string_heap(img, m[0]) : "?";
    const char *mname = "?"; if ((c[1] & 1) && md && (c[1] >> 1)) { mono_metadata_decode_row(md, (int)(c[1] >> 1) - 1, d, 6); mname = mono_metadata_string_heap(img, d[3]); }
    snprintf(b, sizeof b, "P %s%s!%s  (%s)\n", (hit(fn) || hit(mname)) ? "* " : "", mod, fn, mname); wr(b); n_pinv++;
  }
}
static void each_asm(void *as, void *ud) {
  (void)ud; void *img = mono_assembly_get_image(as); if (!img) return; const char *n = (const char *)mono_image_get_name(img); char b[200];
  snprintf(b, sizeof b, "\n## ASSEMBLY %s\n", n ? n : "?"); wr(b);
  types(img, n);
  consts(img, n);
  pinvokes(img);
}
static void *probe_thread(void *a) {
  (void)a; sleep(2);
  static const char *outs[] = { "/user/data/round2/" R2NAME ".txt", "/data/round2/" R2NAME ".txt", 0 }; const char *out = 0;
  for (int i = 0; outs[i] && g_fd < 0; i++) { g_fd = open(outs[i], O_WRONLY | O_CREAT | O_TRUNC, 0666); out = outs[i]; } if (g_fd < 0) return 0;
  wr(R2NAME " 1.00 - read-only: rest mode / wake / network-in-rest names in SceShellUI\n");
  mono_thread_attach(mono_get_root_domain());
  mono_assembly_foreach(each_asm, 0);
  char b[200]; snprintf(b, sizeof b, "\n## end: %d classes, %d methods, %d enums, %d registry constants, %d system functions, %d bytes%s\n", n_cls, n_meth, n_enum, n_reg, n_pinv, g_len, g_len >= MAXOUT - 200 ? " (CUT at the size limit)" : ""); g_len = 0; wr(b);
  close(g_fd); g_fd = -1;
  char dn[120]; snprintf(dn, sizeof dn, "%s.done", out); int d = open(dn, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (d >= 0) { write(d, "1\n", 2); close(d); }
  return 0;
}
static int g_started = 0;
static void go(void) { if (g_started) return; g_started = 1; OrbisPthread th; scePthreadCreate(&th, NULL, (void *(*)(void *))probe_thread, NULL, "r2pwrprobe"); }
s32 attr_public plugin_load(s32 argc, const char *argv[]) { go(); return 0; }
s32 attr_public plugin_unload(s32 argc, const char *argv[]) { return 0; }
s32 attr_module_hidden module_start(s64 argc, const void *args) { go(); return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { return 0; }
