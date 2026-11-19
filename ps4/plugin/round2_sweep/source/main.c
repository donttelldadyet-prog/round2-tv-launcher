// round2_sweep – the ONE read-only menu probe for the per-PS4 sweep (owner 20 Nov 5:49am: "compile all of them … 1 time sweep",
// "log everything since this is exploratory"). Replaces round2_pwrprobe1, which CRASHED Kaeden's menu (CE-36329-3, 20 Nov 5:50am):
// it read the metadata "Constant" table with 3 columns, but Mono's table has 4 (type, padding, parent, value) and Mono stops the
// whole program on a column-count mismatch. Guards now: every table's column count is read from Mono's own table header and
// checked before ANY row is read (mismatch = that part is skipped and logged); a breadcrumb file (<name>.step)
// is rewritten before every step, so after any crash the last step is known.
// Was: round2_pwrprobe – Round2 READ-ONLY probe of the PS4 system menu (SceShellUI) for rest mode, waking and the network in rest.
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
#include <stdlib.h>

#define R2NAME "round2_sweep2"
attr_public const char *g_pluginName = R2NAME;
attr_public const char *g_pluginDesc = "Round2: read-only list of the system menu's rest-mode / wake / network-in-rest functions";
attr_public const char *g_pluginAuth = "Round2";
attr_public u32 g_pluginVersion = 0x00000200;   /* sweep 1.00 = pwrprobe 1.00 + crash guards + breadcrumbs + logout/controller words */

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

#define MAXOUT (3800 * 1024)   /* sweep2: was 1.8 MB, Kaeden 13.50 needed more (app.exe cut) */
static int g_fd = -1, g_len = 0;
static void wr(const char *s) { int n = (int)strlen(s); if (g_fd < 0 || g_len + n > MAXOUT) return; write(g_fd, s, n); g_len += n; }
static void step(const char *a, const char *b2) {   /* breadcrumb: the step about to run (a crashed program keeps what it wrote) */
  int f = open("/user/data/round2/" R2NAME ".step", O_WRONLY | O_CREAT | O_TRUNC, 0666); if (f < 0) return;
  char l[300]; int n = snprintf(l, sizeof l, "%s %s\n", a, b2 ? b2 : ""); write(f, l, n > 0 && n < (int)sizeof l ? n : 0); close(f);
}
extern int mono_table_info_get_rows(void *);
/* Mono's table header: { const char *base; rows:24 | row_size:8; size_bitfield (column count in the top 8 bits) }. The row count
   read here must equal mono_table_info_get_rows() (proves the layout), and the column count must be what we expect. */
static int tbl_ok(void *t, int want, const char *nm) {
  char b[200]; if (!t) { snprintf(b, sizeof b, "# table %s: none\n", nm); wr(b); return 0; }
  unsigned int w0 = *(unsigned int *)((char *)t + 8), w1 = *(unsigned int *)((char *)t + 12);
  int rows = (int)(w0 & 0xffffff), api = mono_table_info_get_rows(t), cols = (int)((w1 >> 24) & 0xff);
  if (rows != api || cols != want) { snprintf(b, sizeof b, "# table %s SKIPPED: rows %d/%d, columns %d (want %d)\n", nm, rows, api, cols, want); wr(b); return 0; }
  return 1;
}

/* words that mark rest mode, waking, power, the network in rest, timers/alarms, Remote Play (it wakes the PS4 too) */
static const char *KEYS[] = { "standby", "restmode", "rest_mode", "inrest", "wakeup", "wake_", "wakeon", "towake", "waking",
  "power", "systemstate", "shutdown", "suspend", "resume", "remoteplay", "remote_play", "keepconn", "stayconn", "netconnect",
  "networkconnect", "connectinrest", "internetin", "autodownload", "autoupdate", "bgdl", "icc", "alarm", "rtc", "schedul",
  "usbsupply", "supplypower", "turnon", "turn_on", "turnoff", "bootfrom", "ddp", "wol", "magic", "sleep", "idle", "dimmer",
  "autooff", "auto_off", "timer", "rpwake",
  "logout", "logoff", "login", "logon", "signin", "signout", "usermgr", "usermanager", "userselect", "selectuser", "switchuser",
  "padmanager", "padinfo", "controller", "psbutton", "homebutton", "quickmenu", "powermenu", "locker", "suppressor", 0 };
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
  if (nc <= 0 || nt <= 0) return;
  if (!tbl_ok(ct, 4, "Constant") || !tbl_ok(ft, 3, "Field") || !tbl_ok(td, 6, "TypeDef")) return;
  unsigned int *fl = (unsigned int *)malloc(sizeof(unsigned int) * (size_t)nt); if (!fl) return;
  for (int i = 0; i < nt; i++) { unsigned int r[6] = { 0 }; mono_metadata_decode_row(td, i, r, 6); fl[i] = r[4]; }
  int last = -1;
  for (int i = 0; i < nc && g_len < MAXOUT; i++) {
    unsigned int c4[4] = { 0 }; mono_metadata_decode_row(ct, i, c4, 4);   /* type, padding, parent, value */
    unsigned int c[3] = { c4[0], c4[2], c4[3] }; if ((c[1] & 3) != 0) continue;   /* only fields */
    unsigned int frow = c[1] >> 2, f[3] = { 0 }; if (!frow || (int)frow > mono_table_info_get_rows(ft)) continue; mono_metadata_decode_row(ft, (int)frow - 1, f, 3);
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
    { char t[24]; snprintf(t, sizeof t, "class row %d", i + 1); step(t, iname); }
    void *k = mono_class_get(img, 0x02000000u | (unsigned)(i + 1)); if (!k) continue;
    const char *cn = mono_class_get_name(k), *ns = mono_class_get_namespace(k);
    int ch = hit(cn) || hit(ns);
    if (mono_class_is_enum(k)) { if (ch) { snprintf(b, sizeof b, "\n### ENUM %s.%s (values under CONSTANTS)\n", ns, cn); wr(b); n_enum++; } continue; }
    if (ch) { full_class(k); n_cls++; continue; }
  }
}
/* method names that match, in classes that don't – straight from the MethodDef table (no class or method is set up). Owner
   class from the TypeDef MethodList column. */
static void methods(void *img) {
  char b[600]; void *md = mono_image_get_table_info(img, 0x06), *td = mono_image_get_table_info(img, 0x02);
  int nm = mono_image_get_table_rows(img, 0x06), nt = mono_image_get_table_rows(img, 0x02); if (nm <= 0 || nt <= 0) return;
  if (!tbl_ok(md, 6, "MethodDef") || !tbl_ok(td, 6, "TypeDef")) return;
  unsigned int *ml = (unsigned int *)malloc(sizeof(unsigned int) * (size_t)nt); if (!ml) return;
  for (int i = 0; i < nt; i++) { unsigned int r[6] = { 0 }; mono_metadata_decode_row(td, i, r, 6); ml[i] = r[5]; }
  int last = -1;
  for (int i = 0; i < nm && g_len < MAXOUT; i++) {
    unsigned int d[6] = { 0 }; mono_metadata_decode_row(md, i, d, 6); const char *mn = mono_metadata_string_heap(img, d[3]);
    if (!hit(mn)) continue;
    unsigned int row = (unsigned int)i + 1; int lo = 0, hi = nt - 1, own = 0; while (lo <= hi) { int mid = (lo + hi) / 2; if (ml[mid] <= row) { own = mid; lo = mid + 1; } else hi = mid - 1; }
    unsigned int t[6] = { 0 }; mono_metadata_decode_row(td, own, t, 6);
    const char *cn = mono_metadata_string_heap(img, t[1]), *ns = mono_metadata_string_heap(img, t[2]);
    if (hit(cn) || hit(ns)) continue;   /* whole class already listed */
    if (own != last) { snprintf(b, sizeof b, "\n### IN %s.%s\n", ns, cn); wr(b); last = own; }
    snprintf(b, sizeof b, "  %s%s%s\n", (d[2] & 0x10) ? "static " : "", (d[2] & 0x2000) ? "native " : "", mn); wr(b); n_meth++;
  }
  free(ml);
}
/* every PS4 system function the menu's program calls (P/Invoke "ImplMap" table): module, function name, C# method. All of them
   (not only matching ones) so two firmwares can be compared; matching ones are marked "*". */
static void pinvokes(void *img) {
  char b[600]; void *im = mono_image_get_table_info(img, 0x1C), *mr = mono_image_get_table_info(img, 0x1A), *md = mono_image_get_table_info(img, 0x06);
  int n = mono_image_get_table_rows(img, 0x1C); if (n <= 0) return;
  if (!tbl_ok(im, 4, "ImplMap") || !tbl_ok(mr, 1, "ModuleRef") || !tbl_ok(md, 6, "MethodDef")) return;
  for (int i = 0; i < n && g_len < MAXOUT; i++) {
    unsigned int c[4] = { 0 }, m[1] = { 0 }, d[6] = { 0 };
    mono_metadata_decode_row(im, i, c, 4);
    const char *fn = mono_metadata_string_heap(img, c[2]);
    if (c[3] && (int)c[3] <= mono_table_info_get_rows(mr)) mono_metadata_decode_row(mr, (int)c[3] - 1, m, 1);
    const char *mod = (c[3] && (int)c[3] <= mono_table_info_get_rows(mr)) ? mono_metadata_string_heap(img, m[0]) : "?";
    const char *mname = "?"; if ((c[1] & 1) && (c[1] >> 1) && (int)(c[1] >> 1) <= mono_table_info_get_rows(md)) { mono_metadata_decode_row(md, (int)(c[1] >> 1) - 1, d, 6); mname = mono_metadata_string_heap(img, d[3]); }
    snprintf(b, sizeof b, "P %s%s!%s  (%s)\n", (hit(fn) || hit(mname)) ? "* " : "", mod, fn, mname); wr(b); n_pinv++;
  }
}
static void each_asm(void *as, void *ud) {
  (void)ud; void *img = mono_assembly_get_image(as); if (!img) return; const char *n = (const char *)mono_image_get_name(img); char b[200];
  snprintf(b, sizeof b, "\n## ASSEMBLY %s\n", n ? n : "?"); wr(b);
  step("types", n); types(img, n);
  step("methods", n); methods(img);
  step("constants", n); consts(img, n);
  step("system functions", n); pinvokes(img);
  if (g_fd >= 0) fsync(g_fd);
}
static void *probe_thread(void *a) {
  (void)a; sleep(2);
  static const char *outs[] = { "/user/data/round2/" R2NAME ".txt", "/data/round2/" R2NAME ".txt", 0 }; const char *out = 0;
  for (int i = 0; outs[i] && g_fd < 0; i++) { g_fd = open(outs[i], O_WRONLY | O_CREAT | O_TRUNC, 0666); out = outs[i]; } if (g_fd < 0) return 0;
  wr(R2NAME " 2.00 - read-only sweep: rest mode / wake / network-in-rest / log-in / controller names in SceShellUI\n");
  step("attach", 0); mono_thread_attach(mono_get_root_domain());
  mono_assembly_foreach(each_asm, 0);
  char b[200]; snprintf(b, sizeof b, "\n## end: %d classes, %d methods, %d enums, %d registry constants, %d system functions, %d bytes%s\n", n_cls, n_meth, n_enum, n_reg, n_pinv, g_len, g_len >= MAXOUT - 200 ? " (CUT at the size limit)" : ""); g_len = 0; wr(b);
  fsync(g_fd); close(g_fd); g_fd = -1; step("finished", 0);
  char dn[120]; snprintf(dn, sizeof dn, "%s.done", out); int d = open(dn, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (d >= 0) { write(d, "1\n", 2); close(d); }
  return 0;
}
static int g_started = 0;
static void go(void) { if (g_started) return; g_started = 1; OrbisPthread th; scePthreadCreate(&th, NULL, (void *(*)(void *))probe_thread, NULL, "r2sweep"); }
s32 attr_public plugin_load(s32 argc, const char *argv[]) { go(); return 0; }
s32 attr_public plugin_unload(s32 argc, const char *argv[]) { return 0; }
s32 attr_module_hidden module_start(s64 argc, const void *args) { go(); return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { return 0; }
