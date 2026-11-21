/* 0.11 (owner 20 Nov 9:02pm: "the tile closed too soon, after 1 pulse"): the engine can say "Ready" for a moment BEFORE the
   saves start loading (the phone's Load was accepted, the clean-slate state still showed the last run as done) – 0.10 took that as
   the end and closed while Aliff's load (20:59:57–21:00:23) was still running. Now the end only counts after the tile has SEEN the
   saves loading (a bar with games), and if it goes back to loading the ready card is cancelled. A load with nothing to copy (no
   bar ever) ends after "Ready" has stayed 10 s.
   0.10 (20 Nov 8:50pm, 3-player test: two GTA V loads failed, the engine said "Sorry" (status off + a title) instead of "done",
   so 0.09 never closed and the add-on waited its full 40 s): a load the tile watched that ends in "Sorry, …" closes the tile too
   (same "What happens next" card – players still pick their profiles).
   0.09 (owner 20 Nov 8:40pm on 0.08: "it doesnt pulse … very static … loading bar doesnt increase gradually its just 0 or 100 …
   huge gap after ready"): the logo now visibly breathes ALL the time (fades towards the background and back, room colour + a wide
   white shine), faster while loading; the bar creeps forward inside each game (eases towards ~90 % of the next step, then jumps
   when the game is really done); after Ready: 1.5 s burst + 6 s "What happens next" (was 3 + 9).
   0.08 (owner 20 Nov 8:05pm: "animation before and after ready, maybe use round2 logo … give instructions for the controllers next
   screen cos they dont know whats gonna happen … immediately after close of ps4 tile"):
   - while saves load the Round2 logo pulses in the room's colour with a white shine sweeping across it;
   - when a load this tile watched finishes: 3 s "ready" burst (rings from the logo, logo flashes green), then 9 s of "What happens
     next" (screen closes · TV shows "Press PS button" · press PS · pick your profile · more players each press their own PS) with a
     countdown bar, then the tile CLOSES ITSELF. The menu add-on logs everyone out as soon as it sees the tile gone.
   - only after a load it watched: a tile opened later (to switch players) never closes by itself (owner: no idle close).
   - asks the engine every 3 s while loading (was 8).
   0.07 (owner 20 Nov 3:03am): no character picture (names went behind the QR code) – text column 130-1070, QR card on the
   right at 1250; the title wraps; the bar label says which player is loading ("player 2 of 3").
   0.06 (owner 20 Nov 2:33am: "can the ps4 tile when launched also show a similar theme, also needs loading bars or something
   responsive to have feedback to guests"): the room's colour + character (like the member page and the TV home screen), a glow, a
   moving progress bar (n of m games, or a sliding bar while it waits), animated dots and a pulsing frame round the QR code.
   The screen is composed once into BASE when something changes; each frame copies it and draws only the moving parts.
   0.05: the title line comes from the engine (member loading / ready). 0.04: PS4 id from the helper's file (the app can't ask the kernel) – 0.03 showed "https/http 400" = no id.
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
#include <time.h>
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
unsigned char *stbi_load_from_memory(unsigned char const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
}

#define W 1920
#define H 1080
#define TILE_V "0.13"
#include <orbis/SystemService.h>
#define ENGINE_HOST "round2-book.donttelldadyet.workers.dev"

std::stringstream debugLogStream;

static Color BG = { 6, 5, 10 }, CARD = { 20, 17, 31 }, INK = { 244, 241, 251 }, DIM = { 167, 159, 189 }, GOLD = { 245, 196, 0 },
             WHITE = { 255, 255, 255 }, BLACK = { 0, 0, 0 }, RED = { 255, 122, 122 }, GREEN = { 125, 255, 168 };

/* ---- what the engine said last (shared with the poll thread) ---- */
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static char g_title[80] = "", g_room[32] = "", g_url[200] = "", g_line[200] = "Connecting to Round2...", g_line2[200] = "", g_status[24] = "wait", g_err[64] = "";
static volatile int g_dirty = 2;
static char g_color[8] = "", g_rname[32] = "", g_done[12] = "", g_total[12] = "", g_pk[8] = "", g_pn[8] = "", g_pnow[32] = "";   /* 0.06/0.07 */
static char g_board[640] = "";
/* 0.13 (owner 22 Nov 00:39: "the ps4 tile should be listening to the helper … it then can know progress reliably"): the helper
   (1.1.149+) writes /data/round2/tile-live.txt on this PS4 – state l(oading) / d(one) / i(dle), the profile board and the current
   player's games – the tile reads it 4 times a second. Fresh (< 30 s) = it wins over the engine's answer, which comes every 3-20 s. */
static int lv_ok = 0, lv_pd = 0, lv_pt = 0; static char lv_state = 'i', lv_board[640] = "", lv_raw[900] = "";
static int live_read(void) {   /* 1 = something changed */
  FILE *f = fopen("/data/round2/tile-live.txt", "r"); if (!f) f = fopen("/user/data/round2/tile-live.txt", "r");
  char b[900] = ""; if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
  long long t = 0; char st = 'i', bd[640] = ""; int pd = 0, pt = 0; char *p = b;
  while (*p) { char *e = strchr(p, '\n'); if (e) *e = 0;
    if (!strncmp(p, "t ", 2)) t = atoll(p + 2); else if (!strncmp(p, "state ", 6)) st = p[6]; else if (!strncmp(p, "board ", 6)) snprintf(bd, sizeof bd, "%s", p + 6); else if (!strncmp(p, "prog ", 5)) sscanf(p + 5, "%d %d", &pd, &pt);
    if (!e) break; p = e + 1; }
  long long now = (long long)time(NULL); int ok = t > 0 && now - t < 30 && t - now < 30 && st != 'i';
  int ch = ok != lv_ok || (ok && (st != lv_state || strcmp(bd, lv_board) || pd != lv_pd || pt != lv_pt));
  lv_ok = ok; lv_state = st; snprintf(lv_board, sizeof lv_board, "%s", bd); lv_pd = pd; lv_pt = pt; (void)lv_raw; return ch;
}   /* 0.12: the room's profiles during a member load ("st~name~new name~member|…", helper 1.1.148) */
static unsigned char *g_art = 0; static int g_aw = 0, g_ah = 0; static char g_artroom[32] = "";
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
      char room[32], u[200], l[200], l2[200], st[24], ti[80];
      char co[8], rn[32], dn[12], tt[12]; jget(buf, "color", co, sizeof co); jget(buf, "rname", rn, sizeof rn); jget(buf, "done", dn, sizeof dn); jget(buf, "total", tt, sizeof tt);
      char pk[8], pn[8], pw[32]; jget(buf, "pk", pk, sizeof pk); jget(buf, "pn", pn, sizeof pn); jget(buf, "pnow", pw, sizeof pw);
      if (strcmp(co, g_color) || strcmp(rn, g_rname) || strcmp(dn, g_done) || strcmp(tt, g_total) || strcmp(pk, g_pk) || strcmp(pn, g_pn) || strcmp(pw, g_pnow)) { strcpy(g_color, co); strcpy(g_rname, rn); strcpy(g_done, dn); strcpy(g_total, tt); strcpy(g_pk, pk); strcpy(g_pn, pn); strcpy(g_pnow, pw); g_dirty = 2; }
      { static char bd[640]; jget(buf, "board", bd, sizeof bd); if (strcmp(bd, g_board)) { strcpy(g_board, bd); g_dirty = 2; } }   /* 0.12 */
      jget(buf, "title", ti, sizeof ti); jget(buf, "room", room, sizeof room); jget(buf, "url", u, sizeof u); jget(buf, "line", l, sizeof l); jget(buf, "line2", l2, sizeof l2); jget(buf, "status", st, sizeof st);
      if (strcmp(room, g_room) || strcmp(u, g_url) || strcmp(l, g_line) || strcmp(l2, g_line2) || strcmp(st, g_status) || strcmp(ti, g_title) || g_err[0]) {
        strcpy(g_title, ti); strcpy(g_room, room); strcpy(g_url, u); strcpy(g_line, l); strcpy(g_line2, l2); strcpy(g_status, st); g_err[0] = 0; g_dirty = 2; }
    } else {
      char e[64]; if (!g_id[0]) snprintf(e, sizeof e, "this PS4's id not found - is the Round2 helper running?"); else if (!net_ok) snprintf(e, sizeof e, "network start 0x%08x", (unsigned)net_err); else snprintf(e, sizeof e, "https 0x%08x  http 0x%08x", (unsigned)n1, (unsigned)n);
      if (strcmp(g_status, "off") || strcmp(e, g_err)) { strcpy(g_status, "off"); strcpy(g_line, "Cannot reach Round2 right now - trying again..."); g_line2[0] = 0; strcpy(g_err, e); g_dirty = 2; }
    }
    pthread_mutex_unlock(&mx);
    sceKernelSleep(strcmp(g_status, "loading") && strcmp(g_status, "reset") ? 20 : 3);   /* faster while something is moving (0.08: 3 s) */
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
/* 0.08: the logo in colour c at strength a (0-255), plus a white shine band centred at sx (logo x) */
static void logo_fx(int x, int y, Color c, int a, int sx, int fade = 0) {   /* 0.09: fade = how far the logo sinks into the background */
  for (int j = 0; j < LOGO_H; j++) for (int i = 0; i < LOGO_W; i++) { int la = LOGO_A[j * LOGO_W + i]; if (!la) continue;
    if (fade) blend(x + i, y + j, BG, la * fade / 255);
    blend(x + i, y + j, c, la * a / 255); int d = abs(i - sx + (j - LOGO_H / 2) / 3); if (d < 70) blend(x + i, y + j, WHITE, la * (70 - d) / 75); } }
static void ring(int cx, int cy, int r, int t, Color c, int a) {
  if (r <= 0 || a <= 0) return; int r2o = (r + t) * (r + t), r2i = r * r;
  for (int y = cy - r - t; y <= cy + r + t; y++) for (int x = cx - r - t; x <= cx + r + t; x++) { int d = (x - cx) * (x - cx) + (y - cy) * (y - cy); if (d >= r2i && d <= r2o) blend(x, y, c, a); } }
static double now_s(void) { return sceKernelGetProcessTime() / 1e6; }
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

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }
static Color hexc(const char *h, Color d) { if (strlen(h) != 6) return d; Color c = { (uint8_t)(hexv(h[0]) * 16 + hexv(h[1])), (uint8_t)(hexv(h[2]) * 16 + hexv(h[3])), (uint8_t)(hexv(h[4]) * 16 + hexv(h[5])) }; return c; }
static void disc(int cx, int cy, int r, Color c, int a) { for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) { int d = x * x + y * y; if (d <= r * r) blend(cx + x, cy + y, c, d > (r - 1) * (r - 1) ? a / 2 : a); } }
static Color mixc(Color a, Color b, float k) { Color c = { (uint8_t)(a.r + (b.r - a.r) * k), (uint8_t)(a.g + (b.g - a.g) * k), (uint8_t)(a.b + (b.b - a.b) * k) }; return c; }
static void frame_box(int x, int y, int w, int h, int t, Color c, int a) { for (int k = 0; k < t; k++) { for (int i = x - k; i < x + w + k; i++) { blend(i, y - k - 1, c, a); blend(i, y + h + k, c, a); } for (int j = y - k; j < y + h + k; j++) { blend(x - k - 1, j, c, a); blend(x + w + k, j, c, a); } } }

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  auto scene = new Scene2D(W, H, 4);
  if (!scene->Init(0xC000000, 2)) for (;;) sceKernelSleep(1);
  FT_Face fBig, fMid, fSmall, fTiny, fPix, fPixS;
  scene->InitFont(&fBig, "/app0/assets/fonts/ChakraPetch-SemiBold.ttf", 72);
  scene->InitFont(&fMid, "/app0/assets/fonts/ChakraPetch-Regular.ttf", 44);
  scene->InitFont(&fSmall, "/app0/assets/fonts/ChakraPetch-SemiBold.ttf", 34);
  scene->InitFont(&fTiny, "/app0/assets/fonts/ChakraPetch-Regular.ttf", 24);
  scene->InitFont(&fPix, "/app0/assets/fonts/PressStart2P-Regular.ttf", 64);
  scene->InitFont(&fPixS, "/app0/assets/fonts/PressStart2P-Regular.ttf", 20);

  find_id();
  net_init();
  pthread_t th; pthread_create(&th, NULL, poller, NULL);

  uint32_t *BASE = (uint32_t *)malloc(W * H * 4);
  int frame = 0, busy = 0, done = 0, total = 0; char st[24] = "wait"; Color AC = GOLD; float shown = 0;
  const int BX = 130, BY = 900, BW = 940, BH = 22;          /* progress bar */
  const int QX = 1270, QY = 230, QS = 480;                   /* QR card (0.07: right, clear of the text) */
  char pkk[8] = "", pnn[8] = "", pnw[32] = "";
  int seen_loading = 0, howto = 0, seen_bar = 0; double ready_t0 = 0, done_since = 0;   /* 0.08; 0.11: seen_bar, done_since */
  /* 0.12 (owner 21 Nov 11:54pm): profile cards – main + Player 1-4 start grey, each turns the room colour in order as it's ready,
     a Player card changes into the member's name; the one being worked on pulses so guests can see something is happening */
  int bn = 0, was_busy = 0, lit[5] = {0}; double lit_t[5] = {0}, last_lit = 0; char bst[5] = {0}, bcur[5][32], btgt[5][32], bwho[5][32];
  const int CX0 = 1210, CY0 = 196, CW = 580, CH = 118, CG = 14;
  FT_Face fF = fMid;
  for (;;) {
    if (frame % 15 == 0 && live_read()) g_dirty = 2;   /* 0.13 */
    if (g_dirty > 0 && BASE && !howto) {   /* 0.08: the instruction card stays until the tile closes */
      char room[32], url[200], line[200], line2[200], err[64], ti[80], co[8], rn[32];
      pthread_mutex_lock(&mx); strcpy(ti, g_title); strcpy(room, g_room); strcpy(url, g_url); strcpy(line, g_line); strcpy(line2, g_line2); strcpy(st, g_status); strcpy(err, g_err);
      strcpy(co, g_color); strcpy(rn, g_rname); done = atoi(g_done); total = atoi(g_total); strcpy(pkk, g_pk); strcpy(pnn, g_pn); strcpy(pnw, g_pnow); g_dirty--;
      pthread_mutex_unlock(&mx);
      if (lv_ok) { strcpy(st, lv_state == 'l' ? "loading" : "done"); }   /* 0.13: the helper on this PS4 knows best */
      FB = BASE; AC = hexc(co, GOLD); busy = !strcmp(st, "loading") || !strcmp(st, "reset");
      { char bd[640]; pthread_mutex_lock(&mx); strcpy(bd, g_board); pthread_mutex_unlock(&mx); if (lv_ok) strcpy(bd, lv_board); bn = 0; char *p = bd;   /* 0.12 */
        while (*p && bn < 5) { char *e = strchr(p, '|'); if (e) *e = 0; char *f[4] = { p, (char *)"", (char *)"", (char *)"" }; int k = 1;
          for (char *q = p; *q && k < 4; q++) if (*q == '~') { *q = 0; f[k++] = q + 1; }
          bst[bn] = f[0][0]; snprintf(bcur[bn], 32, "%s", f[1]); snprintf(btgt[bn], 32, "%s", f[2][0] ? f[2] : f[1]); snprintf(bwho[bn], 32, "%s", f[3]); bn++;
          if (!e) break; p = e + 1; }
        if (busy && !was_busy) { memset(lit, 0, sizeof lit); last_lit = 0; } was_busy = busy; }
      if (lv_ok && bn > 0) { int nt = 0, nd = 0, li = -1, ci = -1; for (int i = 0; i < bn; i++) { char c = bst[i]; if (c == 'w' || c == 'l' || c == 'd' || c == 'f') nt++; if (c == 'd' || c == 'f') nd++; if (c == 'l') li = i; if (c == 'c') ci = i; }   /* 0.13: our own words */
        if (lv_state == 'l') { if (li >= 0) snprintf(line, sizeof line, "Loading %s's saves%s. Please don't start a game yet.", bwho[li], nt > 1 ? (snprintf(line2, sizeof line2, " (player %d of %d)", nd + 1, nt), line2) : "");
          else if (ci >= 0) snprintf(line, sizeof line, "Getting the profiles ready first. Please don't start a game yet.");
          snprintf(line2, sizeof line2, "Watch the profiles on the right - each one lights up when it's ready."); }
        else if (!ti[0] || strncmp(ti, "Sorry", 5)) { snprintf(ti, sizeof ti, "All set!"); snprintf(line, sizeof line, "Your saves are on your profiles."); line2[0] = 0; } }
      if (busy) { seen_loading = 1; if (total > 0) seen_bar = 1; if (ready_t0 && !howto) ready_t0 = 0; done_since = 0; }   /* 0.11: back to loading = not the end */
      { int fin = !strcmp(st, "done") || (!strcmp(st, "off") && !strncmp(ti, "Sorry", 5));
        if (fin && seen_loading && !done_since) done_since = now_s();
        if (fin && seen_loading && !ready_t0 && (seen_bar || now_s() - done_since >= 10)) ready_t0 = now_s(); }   /* 0.08/0.10/0.11 */
      for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {   /* background + the room-colour glow from the top right */
        int dx = x - 1650, dy = y + 150; float d = (float)(dx * dx + dy * dy) / (1350.f * 1350.f); float g = d < 1 ? (1 - d) * (1 - d) * 0.34f : 0;
        int r = BG.r + (int)((AC.r - BG.r) * g), gg = BG.g + (int)((AC.g - BG.g) * g), b = BG.b + (int)((AC.b - BG.b) * g);
        BASE[y * W + x] = 0xFF000000u | (r << 16) | (gg << 8) | b; }
      rect(0, 0, W, 12, AC);
      logo(130, 90, WHITE);
      text(fPixS, "MEMBERS", 136, 318, AC);
      char up[32]; snprintf(up, sizeof up, "%s", rn[0] ? rn : room); for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
      if (up[0]) text(fPix, up, 130, 410, AC);
      int y = wrap(fBig, ti[0] ? ti : busy ? "Please wait" : "Scan to log in", 130, 520, 940, 84, !strcmp(st, "done") ? GREEN : INK);
      y = wrap(fMid, line, 130, y + 10, 940, 56, busy ? AC : INK);
      if (line2[0]) wrap(fSmall, line2, 130, y + 14, 940, 44, DIM);
      if (err[0]) text(fTiny, err, 130, 1050, DIM);
      { char vv[48]; snprintf(vv, sizeof vv, "tile " TILE_V "  id %s", g_idsrc); text(fTiny, vv, 1640, 1060, DIM); }
      int cards = bn > 0 && (busy || !strcmp(st, "done"));   /* 0.12: the profile cards take the QR's place during a load */
      if (cards) { text(fPixS, "PROFILES", CX0, CY0 - 30, AC); }
      else { rect(QX - 20, QY - 20, QS + 40, QS + 40, CARD); rect(QX - 20, QY - 20, QS + 40, 6, AC); }
      if (cards) {} else if (url[0]) { draw_qr(url, QX, QY, QS); int tw2 = text(fSmall, "Scan with your phone camera", 0, 0, INK, 0); text(fSmall, "Scan with your phone camera", QX + QS / 2 - tw2 / 2, QY + QS + 80, INK);
        int tw3 = text(fTiny, "book.round2.sg/m", 0, 0, DIM, 0); text(fTiny, "book.round2.sg/m", QX + QS / 2 - tw3 / 2, QY + QS + 122, DIM); }
      else { text(fSmall, "QR code appears when", QX + 60, QY + 210, DIM); text(fSmall, "Round2 answers", QX + 100, QY + 260, DIM); }
      if (busy || !strcmp(st, "done")) rect(BX, BY, BW, BH, CARD);
      else text(fSmall, "Members: scan, log in, tick who's playing.", 130, 990, DIM);
    }
    /* each frame: the composed screen, then the moving parts */
    FB = scene->Buf(); if (BASE) memcpy(FB, BASE, W * H * 4);
    if (bn > 0 && !howto && (busy || !strcmp(st, "done"))) {   /* 0.12: profile cards */
      double t = now_s(); Color GREY = { 58, 54, 72 }, PULSE = AC;
      for (int i = 0; i < bn; i++) { if (lit[i]) continue; char c = bst[i]; if ((c == 'd' || c == 'f' || c == 'u') && t - last_lit >= 0.45) { lit[i] = 1; lit_t[i] = t; last_lit = t; } break; }
      for (int i = 0; i < bn; i++) {
        int y = CY0 + i * (CH + CG), L = lit[i], active = !L && (bst[i] == 'c' || bst[i] == 'l'); double since = L ? t - lit_t[i] : 0;
        rect(CX0, y, CW, CH, CARD); rect(CX0, y, 8, CH, L ? AC : GREY);
        if (L && since < 0.6) { int a = (int)(110 * (1 - since / 0.6)); for (int yy = y; yy < y + CH; yy++) for (int xx = CX0; xx < CX0 + CW; xx++) blend(xx, yy, AC, a); }
        if (active) { double ph = (t - (int)(t / 1.6) * 1.6) / 1.6; int sx = CX0 - 120 + (int)(ph * (CW + 240));
          for (int xx = sx - 70; xx < sx + 70; xx++) { if (xx < CX0 || xx >= CX0 + CW) continue; int a = 34 - abs(xx - sx) * 34 / 70; for (int yy = y; yy < y + CH; yy++) blend(xx, yy, WHITE, a); }
          int pa = 110 + (int)(100 * (ph < 0.5 ? ph * 2 : 2 - ph * 2)); frame_box(CX0, y, CW, CH, 3, PULSE, pa); }
        int cx = CX0 + 72, cy = y + CH / 2, r = 40; const char *nm = L ? btgt[i] : bcur[i];
        disc(cx, cy, r, L ? AC : GREY, 255);
        if (active) { double ph = (t - (int)(t / 1.2) * 1.2) / 1.2; ring(cx, cy, r + 4 + (int)(ph * 26), 3, AC, (int)(190 * (1 - ph))); }
        if (L && since < 0.9) ring(cx, cy, r + (int)(since * 130), 5, AC, (int)(220 * (1 - since / 0.9)));
        { char in[2] = { (char)(nm[0] >= 'a' && nm[0] <= 'z' ? nm[0] - 32 : nm[0] ? nm[0] : '?'), 0 }; int w2 = text(fSmall, in, 0, 0, INK, 0); text(fSmall, in, cx - w2 / 2, cy + 13, L ? BG : DIM); }
        int nx = CX0 + 136, ny = y + 52;
        if (L && strcmp(bcur[i], btgt[i]) && since < 0.55) { float k = (float)(since / 0.55);   /* the Player card turns into the member */
          text(fSmall, bcur[i], nx, ny - (int)(k * 26), mixc(DIM, CARD, k)); text(fSmall, btgt[i], nx, ny + 26 - (int)(k * 26), mixc(CARD, INK, k)); }
        else text(fSmall, nm, nx, ny, L ? INK : DIM);
        char ln[80]; Color lc = DIM; char c = bst[i];
        if (L) { if (c == 'f') { snprintf(ln, sizeof ln, "Ready - some games failed, staff will check"); lc = RED; } else { snprintf(ln, sizeof ln, c == 'd' && bwho[i][0] && !strcmp(bcur[i], btgt[i]) && i == 0 ? "Ready - %s's saves are in" : "Ready", bwho[i]); lc = GREEN; } }
        else if (c == 'c') { snprintf(ln, sizeof ln, "Clearing for the next player..."); lc = AC; }
        else if (c == 'l') { snprintf(ln, sizeof ln, "Loading %s's saves...", bwho[i][0] ? bwho[i] : "the"); lc = AC; }
        else if (c == 'w') snprintf(ln, sizeof ln, "Waiting - %s is next", bwho[i][0] ? bwho[i] : "a player");
        else snprintf(ln, sizeof ln, "Waiting");
        if (active) { int n = (int)(t * 2.5) % 4; size_t l = strlen(ln); while (l && ln[l - 1] == '.') ln[--l] = 0; for (int d = 0; d < n && l < sizeof ln - 1; d++) ln[l++] = '.'; ln[l] = 0; }
        text(fTiny, ln, nx, y + 92, lc);
        if (L) { int kx = CX0 + CW - 54, ky = y + CH / 2; float g = since < 0.35 ? (float)(since / 0.35) : 1.f;   /* check mark draws itself */
          for (int s2 = 0; s2 < (int)(14 * g); s2++) rect(kx + s2, ky + s2 / 1, 5, 5, c == 'f' ? RED : GREEN);
          if (g >= 1) for (int s2 = 0; s2 < 26; s2++) rect(kx + 13 + s2, ky + 13 - s2, 5, 5, c == 'f' ? RED : GREEN); }
      }
    }
    if (busy) {
      char lab[96]; int lx;
      int bnt = 0, bnd = 0, bli = 0; for (int i = 0; i < bn; i++) { char c = bst[i]; if (c == 'w' || c == 'l' || c == 'd' || c == 'f') bnt++; if (c == 'd' || c == 'f') bnd++; if (c == 'l') bli = 1; }
      if (bn > 0 && bnt > 0) {   /* 0.13 (owner: 1 of 3 profiles in = the bar a third full): the bar is the whole load, player by player */
        int pd = lv_ok ? lv_pd : done, pt = lv_ok ? lv_pt : total; static int lastk = -1; static double since2 = 0; int key = bnd * 100 + pd; if (key != lastk) { lastk = key; since2 = now_s(); }
        double tt = now_s() - since2; float inside = bli && pt > 0 ? ((float)pd + (float)(0.9 * (1.0 - 1.0 / (1.0 + tt / 20.0)))) / pt : (bli ? (float)(0.5 * (1.0 - 1.0 / (1.0 + tt / 15.0))) : 0);
        if (inside > 0.98f) inside = 0.98f; float want = ((float)bnd + inside) / bnt; shown += (want - shown) * 0.06f; if (shown > want) shown = want;
        int fw = BW * shown; if (fw < 26) fw = 26; rect(BX, BY, fw, BH, AC);
        int sx = (frame * 9) % (fw + 160) - 160; for (int x = sx; x < sx + 160; x++) if (x >= 0 && x < fw) { int a = 90 - abs(x - sx - 80) * 90 / 80; for (int j = 0; j < BH; j++) blend(BX + x, BY + j, WHITE, a); }
        snprintf(lab, sizeof lab, "%d of %d profile%s ready  -  %d%%", bnd, bnt, bnt == 1 ? "" : "s", (int)(shown * 100)); }
      else if (total > 0) { static int lastdone = -1; static double since = 0; if (done != lastdone) { lastdone = done; since = now_s(); }
        double t = now_s() - since; float creep = done < total ? (float)(0.9 * (1.0 - 1.0 / (1.0 + t / 20.0))) : 0;   /* 0.09: inside a game */
        float want = ((float)done + creep) / total; shown += (want - shown) * 0.06f; if (shown > want) shown = want;
        int fw = BW * shown; if (fw < 26) fw = 26; rect(BX, BY, fw, BH, AC);
        int sx = (frame * 9) % (fw + 160) - 160; for (int x = sx; x < sx + 160; x++) if (x >= 0 && x < fw) { int a = 90 - abs(x - sx - 80) * 90 / 80; for (int j = 0; j < BH; j++) blend(BX + x, BY + j, WHITE, a); }
        if (pnn[0] && atoi(pnn) > 1) snprintf(lab, sizeof lab, "%s (player %s of %s)  -  %d of %d games", pnw, pkk, pnn, done, total); else snprintf(lab, sizeof lab, "%d of %d games  -  %d%%", done, total, done * 100 / total); }
      else { int span = BW - 220, p = (frame * 7) % (2 * span), x = p < span ? p : 2 * span - p; rect(BX + x, BY, 220, BH, AC); snprintf(lab, sizeof lab, "Getting ready"); }
      lx = text(fSmall, lab, BX, BY + 70, INK);
      char dots[8]; int n = (frame / 20) % 4; for (int i = 0; i < n; i++) dots[i] = '.'; dots[n] = 0; text(fSmall, dots, lx + 4, BY + 70, AC);
      { int ph = frame % 60 < 30 ? frame % 30 : 30 - frame % 30; logo_fx(130, 90, AC, 90 + ph * 5, (frame * 14) % (LOGO_W + 400) - 200, 160 - ph * 5); }   /* 0.09: breathes fast while loading */
    } else if (!strcmp(st, "done") && !howto) { shown = 0; rect(BX, BY, BW, BH, GREEN); text(fSmall, "All set - enjoy!", BX, BY + 70, GREEN); }
    if (!ready_t0 && done_since && seen_loading && now_s() - done_since >= 10 && (!strcmp(st, "done") || !strcmp(st, "off"))) ready_t0 = now_s();   /* 0.11: nothing-to-load fallback */
    if (ready_t0) {   /* 0.08: ready burst, then "what happens next", then close */
      double e = now_s() - ready_t0;
      if (e < 1.5) { int cx = 130 + LOGO_W / 2, cy = 90 + LOGO_H / 2;
        for (int k = 0; k < 3; k++) { double q = e - k * 0.25; if (q > 0) ring(cx, cy, (int)(q * 1000), 6, k == 1 ? AC : GREEN, (int)(200 * (1 - q / 1.5))); }
        logo_fx(130, 90, GREEN, 120 + (int)(120 * (1 - (e / 1.5))), (int)(e * 800) % (LOGO_W + 300) - 150); }
      else if (e < 7.5) {
        if (!howto && BASE) { howto = 1; FB = BASE;   /* compose the instruction card once */
          rect(0, 0, W, H, BG); rect(0, 0, W, 12, AC); logo(130, 90, WHITE);
          int y = wrap(fBig, "What happens next", 130, 380, 1660, 84, GREEN);
          const char *L[] = { "1.  This screen closes by itself.", "2.  Your TV shows \"Press PS button\".", "3.  Press the PS button on your controller.",
                              "4.  Pick your profile - it has your name and picture.", "5.  More players? Each one presses PS on their own controller and picks theirs." };
          y += 20; for (int i = 0; i < 5; i++) y = wrap(fF, L[i], 150, y + 18, 1620, 56, i == 2 || i == 3 ? INK : DIM);
          FB = scene->Buf(); memcpy(FB, BASE, W * H * 4); }
        double left = 7.5 - e; int bw = (int)(1660 * left / 6.0); if (bw < 0) bw = 0;
        rect(130, 960, 1660, 18, CARD); rect(130, 960, bw, 18, AC);
        char cl[48]; snprintf(cl, sizeof cl, "Closing in %d", (int)left + 1); text(fSmall, cl, 130, 1030, DIM);
        logo_fx(130, 90, AC, 60, (frame * 10) % (LOGO_W + 300) - 150); }
      else { sceSystemServiceLoadExec("exit", NULL); exit(0); }
    }
    else { if (!busy) shown = 0;   /* 0.13: was reset every frame, so the bar never got past ~6 % of the way */
      if (!(bn > 0 && (busy || !strcmp(st, "done")))) { int a = 110 + (int)(100 * (frame % 120 < 60 ? frame % 60 : 60 - frame % 60) / 60); frame_box(QX - 20, QY - 20, QS + 40, QS + 40, 4, AC, a); }   /* 0.13: no QR frame over the profile cards */
      if (!ready_t0) { int ph = frame % 180 < 90 ? frame % 90 : 90 - frame % 90; logo_fx(130, 90, AC, ph * 2, (frame * 6) % (LOGO_W + 900) - 300, ph); } }   /* 0.09: gentle breathing when idle */
    scene->SubmitFlip(frame); scene->FrameWait(frame); scene->FrameBufferSwap(); frame++;
  }
  return 0;
}
