/* Round2 tile (PS4 app, fPKG) – v0.1, 19 Nov 2026 (owner: "the tile just opens the page, shows progress, and keeps guests
   from interrupting save prep").
   What it does now: asks the engine every 20 s what this PS4 is doing (GET /ps4/tile?id=<this PS4's id>) and shows
     - a QR code to the member page for this room (book.round2.sg/m?room=<room>) – members log in on their phone,
     - the room's state in plain words: ready / the room is being reset / a member's saves are loading (n of m games) –
       "please don't start a game yet" while saves load.
   Coming next (part 2): pick who's playing, PIN on the PS4 number keypad, "your save is on Karen" hints, pictures.
   Built with the OpenOrbis toolchain (samples/_common Scene2D + FreeType); QR codes by Project Nayuki's qrcodegen (MIT).
   Fonts: Chakra Petch + Press Start 2P (SIL OFL, in assets/fonts). */
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
extern "C" {
#include "qrcodegen.h"
}

#define W 1920
#define H 1080
#define ENGINE "https://round2-book.donttelldadyet.workers.dev"

std::stringstream debugLogStream;

static Color BG = { 6, 5, 10 }, CARD = { 11, 9, 20 }, INK = { 244, 241, 251 }, DIM = { 167, 159, 189 }, GOLD = { 245, 196, 0 },
             WHITE = { 255, 255, 255 }, BLACK = { 0, 0, 0 }, RED = { 255, 122, 122 }, GREEN = { 125, 255, 168 };

/* ---- what the engine said last (shared with the poll thread) ---- */
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static char g_room[32] = "", g_url[200] = "", g_line[200] = "Connecting to Round2...", g_line2[200] = "", g_status[24] = "wait";
static volatile int g_dirty = 2;
static char g_id[40] = "";

static int net_ok = 0, httpCtx = 0;
static int skipSsl(int, unsigned int, void *const[], int, void *) { return 0; }   /* the PS4's own certificate list is old; the engine answers only status text */
static void net_init(void) {
  if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0) return;
  if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP) < 0) return;
  if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL) < 0) return;
  sceNetInit();
  int mem = sceNetPoolCreate("r2tile", 64 * 1024, 0); if (mem < 0) return;
  int ssl = sceSslInit(256 * 1024); if (ssl < 0) return;
  httpCtx = sceHttpInit(mem, ssl, 256 * 1024); if (httpCtx < 0) return;
  net_ok = 1;
}
static int http_get(const char *url, char *out, int max) {
  int tpl = 0, conn = 0, req = 0, n = 0, code = 0;
  tpl = sceHttpCreateTemplate(httpCtx, "Round2Tile/0.1 (PLAYSTATION 4)", ORBIS_HTTP_VERSION_1_1, 1); if (tpl < 0) return -1;
  sceHttpsSetSslCallback(tpl, skipSsl, NULL);
  conn = sceHttpCreateConnectionWithURL(tpl, url, 1); if (conn < 0) { n = -1; goto done; }
  req = sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, url, 0); if (req < 0) { n = -1; goto done; }
  if (sceHttpSendRequest(req, NULL, 0) < 0 || sceHttpGetStatusCode(req, &code) < 0 || code != 200) { n = -1; goto done; }
  while (n < max - 1) { int r = sceHttpReadData(req, out + n, max - 1 - n); if (r <= 0) break; n += r; }
  out[n < 0 ? 0 : n] = 0;
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
    snprintf(url, sizeof url, ENGINE "/ps4/tile?id=%s", g_id);
    int n = net_ok ? http_get(url, buf, sizeof buf) : -1;
    pthread_mutex_lock(&mx);
    if (n > 0) {
      char room[32], u[200], l[200], l2[200], st[24];
      jget(buf, "room", room, sizeof room); jget(buf, "url", u, sizeof u); jget(buf, "line", l, sizeof l); jget(buf, "line2", l2, sizeof l2); jget(buf, "status", st, sizeof st);
      if (strcmp(room, g_room) || strcmp(u, g_url) || strcmp(l, g_line) || strcmp(l2, g_line2) || strcmp(st, g_status)) {
        strcpy(g_room, room); strcpy(g_url, u); strcpy(g_line, l); strcpy(g_line2, l2); strcpy(g_status, st); g_dirty = 2; }
    } else if (strcmp(g_status, "off")) { strcpy(g_status, "off"); strcpy(g_line, "Cannot reach Round2 right now - trying again..."); g_line2[0] = 0; g_dirty = 2; }
    pthread_mutex_unlock(&mx);
    sceKernelSleep(strcmp(g_status, "loading") && strcmp(g_status, "reset") ? 20 : 8);   /* faster while something is moving */
  }
  return 0;
}

/* the PS4's 16-byte id – the same one the Round2 helper reports. Not in the toolchain's libkernel list, so looked up by name. */
static int open_ps_id(uint8_t *id) {
  int h = sceKernelLoadStartModule("libkernel.sprx", 0, NULL, 0, NULL, NULL); if (h <= 0) h = 0x2001;
  int (*fn)(uint8_t *) = NULL;
  if (sceKernelDlsym(h, "sceKernelGetOpenPsId", (void **)&fn) || !fn) { h = 0x2001; if (sceKernelDlsym(h, "sceKernelGetOpenPsId", (void **)&fn) || !fn) return -1; }
  return fn(id);
}
/* simple word wrap: about `cols` characters a line */
static void draw_wrap(Scene2D *s, const char *txt, FT_Face f, int x, int y, int cols, int lh, Color bg, Color fg) {
  char line[256]; const char *p = txt;
  while (*p) {
    int n = (int)strlen(p); if (n > cols) { n = cols; while (n > 0 && p[n] != ' ') n--; if (!n) n = cols; }
    snprintf(line, sizeof line, "%.*s", n, p); s->DrawText(line, f, x, y, bg, fg); y += lh; p += n; while (*p == ' ') p++;
  }
}
static void draw_qr(Scene2D *s, const char *text, int x, int y, int size) {
  static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
  if (!text[0] || !qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return;
  int q = qrcodegen_getSize(qr), cell = size / (q + 8), off = (size - cell * q) / 2;
  s->DrawRectangle(x, y, size, size, WHITE);
  for (int r = 0; r < q; r++) for (int c = 0; c < q; c++) if (qrcodegen_getModule(qr, c, r)) s->DrawRectangle(x + off + c * cell, y + off + r * cell, cell, cell, BLACK);
}

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  auto scene = new Scene2D(W, H, 4);
  if (!scene->Init(0xC000000, 2)) for (;;) sceKernelSleep(1);
  FT_Face fPix, fBig, fMid, fSmall;
  scene->InitFont(&fPix, "/app0/assets/fonts/PressStart2P-Regular.ttf", 44);
  scene->InitFont(&fBig, "/app0/assets/fonts/ChakraPetch-SemiBold.ttf", 64);
  scene->InitFont(&fMid, "/app0/assets/fonts/ChakraPetch-Regular.ttf", 44);
  scene->InitFont(&fSmall, "/app0/assets/fonts/ChakraPetch-Regular.ttf", 32);

  { uint8_t id[16] = { 0 }; if (!open_ps_id(id)) for (int i = 0; i < 16; i++) sprintf(g_id + i * 2, "%02x", id[i]); }
  net_init();
  pthread_t th; pthread_create(&th, NULL, poller, NULL);

  int frame = 0;
  for (;;) {
    if (g_dirty > 0) {
      char room[32], url[200], line[200], line2[200], st[24];
      pthread_mutex_lock(&mx); strcpy(room, g_room); strcpy(url, g_url); strcpy(line, g_line); strcpy(line2, g_line2); strcpy(st, g_status); g_dirty--; pthread_mutex_unlock(&mx);
      scene->FrameBufferFill(BG);
      scene->DrawRectangle(0, 0, W, 10, GOLD);
      scene->DrawText((char *)"ROUND2", fPix, 140, 200, BG, GOLD);
      char head[64]; snprintf(head, sizeof head, "MEMBERS%s%s", room[0] ? "  -  " : "", room); for (char *c = head; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
      scene->DrawText(head, fSmall, 142, 260, BG, DIM);
      int busy = !strcmp(st, "loading") || !strcmp(st, "reset");
      scene->DrawRectangle(140, 330, 900, 4, busy ? GOLD : (strcmp(st, "off") ? GREEN : RED));
      scene->DrawText((char *)(busy ? "Please wait" : "Scan to log in"), fBig, 140, 450, BG, INK);
      draw_wrap(scene, line, fMid, 140, 560, 40, 62, BG, busy ? GOLD : INK);
      if (line2[0]) draw_wrap(scene, line2, fSmall, 140, 820, 56, 46, BG, DIM);
      scene->DrawText((char *)"Members: open your phone camera and scan the code.", fSmall, 140, 980, BG, DIM);
      if (url[0]) { scene->DrawRectangle(1160, 170, 640, 640, CARD); draw_qr(scene, url, 1180, 190, 600); scene->DrawText((char *)"book.round2.sg/m", fSmall, 1330, 880, BG, DIM); }
    }
    scene->SubmitFlip(frame); scene->FrameWait(frame); scene->FrameBufferSwap(); frame++;
  }
  return 0;
}
