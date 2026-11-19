/* 0.04: PS4 id from the helper's file (the app can't ask the kernel) – 0.03 showed "https/http 400" = no id.
   Round2 tile (PS4 app, fPKG RTWO00001) – owner 19 Nov 2026: "the tile just opens the page, shows progress, and keeps guests
   from interrupting save prep".
   0.02 (owner 11:25pm, after testing 0.01 on Kaeden: text hard to read, "can't reach Round2", use our logo):
     - own text drawing (FreeType glyphs alpha-blended onto the screen; the sample's drawer showed hollow letters) and solid
       colours (the sample wrote half-transparent pixels, which made the screen look purple),
     - the Round2 logo (white) + MEMBERS, also on the home-screen icon,
     - engine call: HTTPS with the PS4's certificate checks off (its certificate list is old), then plain HTTP if that fails;
       the error code shows small at the bottom so a failure can be traced.
   What it shows: a QR code to the member page for this room (book.round2.sg/m?room=<room>) and the room's state in plain
   words – ready / being reset (n of m) / a member's saves loading (n of m, "please don't start a game yet").
   Coming next: pick who's playing, PIN on the PS4 number keypad, "your save is on Karen" hints, pictures, self-update.
   Built with the OpenOrbis toolchain (Scene2D from its samples, GPL-3.0) + FreeType; QR codes by Project Nayuki's qrcodegen
   (MIT). Fonts: Chakra Petch + Press Start 2P (SIL OFL, in assets/fonts). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sstream>

#include <orbis/libkernel.h>
#include <orbis/Http.h>
#include <orbis/Ssl.h>
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>

#include "graphics.h"
#include "log.h"
#include "logo.h"
extern "C" {
#include "qrcodegen.h"
}

#define W 1920
#define H 1080
#define TILE_V "0.04"
#define ENGINE_HOST "round2-book.donttelldadyet.workers.dev"

std::stringstream debugLogStream;

static Color BG = { 6, 5, 10 }, CARD = { 20, 17, 31 }, INK = { 244, 241, 251 }, DIM = { 167, 159, 189 }, GOLD = { 245, 196, 0 },
             WHITE = { 255, 255, 255 }, BLACK = { 0, 0, 0 }, RED = { 255, 122, 122 }, GREEN = { 125, 255, 168 };

/* ---- what the engine said last (shared with the poll thread) ---- */
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static char g_room[32] = "", g_url[200] = "", g_line[200] = "Connecting to Round2...", g_line2[200] = "", g_status[24] = "wait", g_err[64] = "";
static volatile int g_dirty = 2;
static char g_id[40] = "";

/* ---- network ---- */
static int net_ok = 0, httpCtx = 0, net_err = 0;
static void net_init(void) {
  int r;
  if ((r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET)) < 0) { net_err = r; return; }
  if ((r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP)) < 0) { net_err = r; return; }
  if ((r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL)) < 0) { net_err = r; return; }
  sceNetInit();
  int mem = sceNetPoolCreate("r2tile", 64 * 1024, 0); if (mem < 0) { net_err = mem; return; }
  int ssl = sceSslInit(256 * 1024); if (ssl < 0) { net_err = ssl; return; }
  httpCtx = sceHttpInit(mem, ssl, 256 * 1024); if (httpCtx < 0) { net_err = httpCtx; return; }
  net_ok = 1;
}
/* returns bytes read, or a negative PS4 error code */
static int http_get(const char *url, char *out, int max) {
  int tpl = 0, conn = 0, req = 0, n = 0, r = 0; int32_t code = 0;
  tpl = sceHttpCreateTemplate(httpCtx, "Round2Tile/" TILE_V " (PLAYSTATION 4)", ORBIS_HTTP_VERSION_1_1, 1); if (tpl < 0) return tpl;
  if (!strncmp(url, "https", 5)) sceHttpsDisableOption(tpl, 0x3F);   /* server / name / date / known-CA checks off: old CA list on the PS4 */
  conn = sceHttpCreateConnectionWithURL(tpl, url, 1); if (conn < 0) { n = conn; goto done; }
  req = sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, url, 0); if (req < 0) { n = req; goto done; }
  if ((r = sceHttpSendRequest(req, NULL, 0)) < 0) { n = r; goto done; }
  if ((r = sceHttpGetStatusCode(req, &code)) < 0) { n = r; goto done; }
  if (code != 200) { n = -1000 - code; goto done; }
  while (n < max - 1) { int k = sceHttpReadData(req, out + n, max - 1 - n); if (k <= 0) break; n += k; }
  out[n] = 0;
done:
  if (req > 0) sceHttpDeleteRequest(req);
  if (conn > 0) sceHttpDeleteConnection(conn);
  if (tpl > 0) sceHttpDeleteTemplate(tpl);
  return n;
}
/* "key":"value" from the engine's small JSON answer (no nesting needed) */
static void jget(const char *j, const char *k, char *out, int n) {
  char pat[48]; snprintf(pat, sizeof pat, "\"%s\":\"", k); out[0] = 0;
  const char *p = strstr(j, pat); if (!p) return; p += strlen(pat);
  int i = 0; while (*p && *p != '"' && i < n - 1) { if (*p == '\\' && p[1]) p++; out[i++] = *p++; } out[i] = 0;
}
static void *poller(void *) {
  static char buf[4096], url[200];
  for (;;) {
    int n = -1, n1 = 0;
    if (net_ok) {
      snprintf(url, sizeof url, "https://" ENGINE_HOST "/ps4/tile?id=%s", g_id); n = n1 = http_get(url, buf, sizeof buf);
      if (n <= 0) { snprintf(url, sizeof url, "http://" ENGINE_HOST "/ps4/tile?id=%s", g_id); n = http_get(url, buf, sizeof buf); }
    }
    pthread_mutex_lock(&mx);
    if (n > 0) {
      char room[32], u[200], l[200], l2[200], st[24];
      jget(buf, "room", room, sizeof room); jget(buf, "url", u, sizeof u); jget(buf, "line", l, sizeof l); jget(buf, "line2", l2, sizeof l2); jget(buf, "status", st, sizeof st);
      if (strcmp(room, g_room) || strcmp(u, g_url) || strcmp(l, g_line) || strcmp(l2, g_line2) || strcmp(st, g_status) || g_err[0]) {
        strcpy(g_room, room); strcpy(g_url, u); strcpy(g_line, l); strcpy(g_line2, l2); strcpy(g_status, st); g_err[0] = 0; g_dirty = 2; }
    } else {
      char e[64]; if (!g_id[0]) snprintf(e, sizeof e, "this PS4's id not found - is the Round2 helper running?"); else if (!net_ok) snprintf(e, sizeof e, "network start 0x%08x", (unsigned)net_err); else snprintf(e, sizeof e, "https 0x%08x  http 0x%08x", (unsigned)n1, (unsigned)n);
      if (strcmp(g_status, "off") || strcmp(e, g_err)) { strcpy(g_status, "off"); strcpy(g_line, "Cannot reach Round2 right now - trying again..."); g_line2[0] = 0; strcpy(g_err, e); g_dirty = 2; }
    }
    pthread_mutex_unlock(&mx);
    sceKernelSleep(strcmp(g_status, "loading") && strcmp(g_status, "reset") ? 20 : 8);   /* faster while something is moving */
  }
  return 0;
}

/* ---- drawing straight into the frame buffer (alpha-blended) ---- */
static uint32_t *FB;
static inline void blend(int x, int y, Color c, int a) {
  if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
  uint32_t *p = FB + y * W + x;
  if (a >= 255) { *p = 0xFF000000u | (c.r << 16) | (c.g << 8) | c.b; return; }
  uint32_t o = *p; int r = (o >> 16) & 255, g = (o >> 8) & 255, b = o & 255;
  r += (c.r - r) * a / 255; g += (c.g - g) * a / 255; b += (c.b - b) * a / 255;
  *p = 0xFF000000u | (r << 16) | (g << 8) | b;
}
static void rect(int x, int y, int w, int h, Color c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) blend(i, j, c, 255); }
/* one line of text; y = baseline; returns the x after it (draw = 0: just measure) */
static int text(FT_Face f, const char *s, int x, int y, Color c, int draw = 1) {
  for (; *s; s++) {
    unsigned ch = (unsigned char)*s; if (ch >= 128) ch = '?';
    if (FT_Load_Glyph(f, FT_Get_Char_Index(f, ch), FT_LOAD_DEFAULT) || FT_Render_Glyph(f->glyph, ft_render_mode_normal)) continue;
    FT_GlyphSlot g = f->glyph; FT_Bitmap *b = &g->bitmap;
    if (draw) for (int r = 0; r < (int)b->rows; r++) for (int k = 0; k < (int)b->width; k++)
      blend(x + g->bitmap_left + k, y - g->bitmap_top + r, c, ((unsigned char *)b->buffer)[r * b->pitch + k]);
    x += g->advance.x >> 6;
  }
  return x;
}
/* word wrap to a pixel width */
static int wrap(FT_Face f, const char *s, int x, int y, int maxw, int lh, Color c) {
  char line[256] = "", word[128]; const char *p = s;
  while (*p) {
    int n = 0; while (p[n] && p[n] != ' ' && n < 127) n++; snprintf(word, sizeof word, "%.*s", n, p); p += n; while (*p == ' ') p++;
    char test[256]; snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && text(f, test, 0, 0, c, 0) > maxw) { text(f, line, x, y, c); y += lh; snprintf(line, sizeof line, "%s", word); }
    else snprintf(line, sizeof line, "%s", test);
  }
  if (line[0]) { text(f, line, x, y, c); y += lh; }
  return y;
}
static void logo(int x, int y, Color c) { for (int j = 0; j < LOGO_H; j++) for (int i = 0; i < LOGO_W; i++) blend(x + i, y + j, c, LOGO_A[j * LOGO_W + i]); }
static void draw_qr(const char *t, int x, int y, int size) {
  static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
  if (!t[0] || !qrcodegen_encodeText(t, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return;
  int q = qrcodegen_getSize(qr), cell = size / (q + 8), off = (size - cell * q) / 2;
  rect(x, y, size, size, WHITE);
  for (int r = 0; r < q; r++) for (int c = 0; c < q; c++) if (qrcodegen_getModule(qr, c, r)) rect(x + off + c * cell, y + off + r * cell, cell, cell, BLACK);
}
/* the PS4's 16-byte id – the same one the Round2 helper reports. 0.03 on Kaeden: the app may not ask the kernel for it (the
   engine answered 400 = no id). 0.04: first the file the helper (1.1.89+) writes, then the kernel ways. g_idsrc says which. */
static char g_idsrc[16] = "";
static int read_id_file(const char *p) {
  FILE *f = fopen(p, "r"); if (!f) return -1; char b[48] = ""; if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f);
  int n = 0; for (int i = 0; b[i] && n < 32; i++) if ((b[i] >= '0' && b[i] <= '9') || (b[i] >= 'a' && b[i] <= 'f')) g_id[n++] = b[i];
  g_id[n] = 0; return n == 32 ? 0 : -1;
}
static void find_id(void) {
  if (!read_id_file("/user/data/round2/tile-id.txt")) { strcpy(g_idsrc, "file"); return; }
  if (!read_id_file("/data/round2/tile-id.txt")) { strcpy(g_idsrc, "file2"); return; }
  const char *libs[] = { "libkernel.sprx", "/system/common/lib/libkernel_sys.sprx", "libkernel_sys.sprx" };
  const char *fns[] = { "sceKernelGetOpenPsId", "sceKernelGetOpenPsIdForSystem" };
  for (int l = 0; l < 3; l++) { int h = sceKernelLoadStartModule(libs[l], 0, NULL, 0, NULL, NULL); if (h <= 0) continue;
    for (int k = 0; k < 2; k++) { int (*fn)(uint8_t *) = NULL; uint8_t id[16] = { 0 };
      if (!sceKernelDlsym(h, fns[k], (void **)&fn) && fn && !fn(id)) { for (int i = 0; i < 16; i++) sprintf(g_id + i * 2, "%02x", id[i]); snprintf(g_idsrc, sizeof g_idsrc, "k%d%d", l, k); return; } } }
  g_id[0] = 0; strcpy(g_idsrc, "none");
}

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  auto scene = new Scene2D(W, H, 4);
  if (!scene->Init(0xC000000, 2)) for (;;) sceKernelSleep(1);
  FT_Face fBig, fMid, fSmall, fTiny;
  scene->InitFont(&fBig, "/app0/assets/fonts/ChakraPetch-SemiBold.ttf", 72);
  scene->InitFont(&fMid, "/app0/assets/fonts/ChakraPetch-Regular.ttf", 46);
  scene->InitFont(&fSmall, "/app0/assets/fonts/ChakraPetch-SemiBold.ttf", 34);
  scene->InitFont(&fTiny, "/app0/assets/fonts/ChakraPetch-Regular.ttf", 24);

  find_id();
  net_init();
  pthread_t th; pthread_create(&th, NULL, poller, NULL);

  int frame = 0;
  for (;;) {
    if (g_dirty > 0) {
      char room[32], url[200], line[200], line2[200], st[24], err[64];
      pthread_mutex_lock(&mx); strcpy(room, g_room); strcpy(url, g_url); strcpy(line, g_line); strcpy(line2, g_line2); strcpy(st, g_status); strcpy(err, g_err); g_dirty--; pthread_mutex_unlock(&mx);
      FB = scene->Buf();
      rect(0, 0, W, H, BG);
      rect(0, 0, W, 12, GOLD);
      logo(130, 130, WHITE);
      char head[64]; snprintf(head, sizeof head, "MEMBERS%s%s", room[0] ? "  -  " : "", room); for (char *c = head; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
      text(fSmall, head, 150, 360, GOLD);
      int busy = !strcmp(st, "loading") || !strcmp(st, "reset");
      rect(150, 400, 900, 5, busy ? GOLD : (strcmp(st, "off") ? GREEN : RED));
      text(fBig, busy ? "Please wait" : "Scan to log in", 150, 510, INK);
      int y = wrap(fMid, line, 150, 600, 900, 62, busy ? GOLD : INK);
      if (line2[0]) wrap(fSmall, line2, 150, y + 30, 900, 46, DIM);
      text(fSmall, "Open your phone camera and scan the code.", 150, 990, DIM);
      if (err[0]) text(fTiny, err, 150, 1050, DIM);
      { char vv[48]; snprintf(vv, sizeof vv, "tile " TILE_V "  id %s", g_idsrc); text(fTiny, vv, 1640, 1050, DIM); }   /* 0.03: version on screen, to see an automatic update land */
      if (url[0]) { rect(1150, 150, 660, 660, CARD); draw_qr(url, 1180, 180, 600); text(fSmall, "book.round2.sg/m", 1330, 870, DIM); }
      else { rect(1150, 150, 660, 660, CARD); text(fSmall, "QR code appears when", 1270, 460, DIM); text(fSmall, "Round2 answers", 1320, 510, DIM); }
    }
    scene->SubmitFlip(frame); scene->FrameWait(frame); scene->FrameBufferSwap(); frame++;
  }
  return 0;
}
