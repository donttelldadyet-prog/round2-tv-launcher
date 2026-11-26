/* 0.16 (27 Nov 2026 12:40am, Karen's "PS4 save screen v2 – same look as the member page", round2sg-home design/tile/
   save-screen-v2.html; owner 12:38am "my gf just pushed a new design, make it live"): four screens instead of two –
   1 Scan (glass orb with the 7 room marbles orbiting, "SAVE POINT" curved over it, QR with room-colour corners),
   2 Loading ("Hey, <crew>!", the crew card, "Who's playing in <room>" rows with Waiting/Loading/Ready badges, bar),
   3 Ready ("<crew> is all set.", the orb full in the room colour – bounce, sparkles, two rippling rings – rows all Ready),
   4 What happens next ("Ready, player one.", 4 steps with marble numbers sliding in, "Closing in N" ring), then it closes.
   Same data and the same load/close rules as 0.15; only the drawing changed (all in draw.h, the simulator draws the same).
   The ready screen now stays t_burst seconds (default 4, was a 1.5 s burst). Words: design/tile.json (keys explained there). */
/* 0.15 (24 Nov 1:45pm – Karen's TV mockups landed, round2-tv-launcher branch design-tv-tile docs/ps4-tile-design.md): layout matched to
   them – room name top-right, kicker in the pixel font, thinner bar full width under the card with "n of m profiles ready", the
   profiles list as plain rows (name left, Ready / Loading / Next right, thin lines; still lights up in order and turns "Player 1"
   into the member), lines above the footer and the foot note, "[PS]" boxed, QR 300 px with its corners, crew card with the logo
   top-right. Not in the mockups but kept: the moving shine on the row being loaded, the tick, the dots. */
/* 0.14 (24 Nov 2026, design's membership pack: "new scan screen … new signed-in screen … room-colour glow, calm motion" + design
   concern "tile phase 2": words and timings from design/tile.json): all drawing moved to draw.h (shared with the desktop simulator).
   Scan screen: words left ("Pick up where you left off."), a stack of three matte cards in the middle, the QR right with
   room-colour corners ("Members scan to log in" / "New guests scan to join"), "Not saving today? Press PS to play." – it opens with a
   ~1.6 s intro (top line, words rise, cards deal in, corners snap, one light sweep), then only the QR corners and the logo breathe.
   Signed-in screen (a member load): "Welcome back, <crew>." + the crew card (CREW SINCE mm/yy) in the middle + the profiles list
   right + the bar underneath. No version text any more – it shows only next to an error. Words + the ready/closing times come from
   GET /ps4/tilecfg (asked when the tile opens and every 10 min); prices stay empty while the owners decide (pricing on hold). */
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

#include "draw.h"
#define TILE_V "0.17"   /* 0.17 (owner 27 Nov 2:43am: the "Closing in" countdown was cut ~2 s in): a load with nothing to copy waited 10 s before "ready", so ready + what happens next (4 + 6 s) ran past the menu add-on's 20-s wait and its log-out cut it. With the helper's live file: 3 s. */
#include <orbis/SystemService.h>
#define ENGINE_HOST "round2-book.donttelldadyet.workers.dev"

std::stringstream debugLogStream;


/* ---- what the engine said last (shared with the poll thread) ---- */
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static char g_title[80] = "", g_room[32] = "", g_url[200] = "", g_line[200] = "Connecting to Round2...", g_line2[200] = "", g_status[24] = "wait", g_err[64] = "";
static volatile int g_dirty = 2;
static char g_color[8] = "", g_rname[32] = "", g_done[12] = "", g_total[12] = "", g_pk[8] = "", g_pn[8] = "", g_pnow[32] = "";   /* 0.06/0.07 */
static char g_board[640] = "", g_crew[48] = "", g_since[8] = "", g_valid[8] = ""; static TCfg g_cfg;   /* 0.14: crew + since, the words from design/tile.json */
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
      { char cw[48], sn[8], va[8]; jget(buf, "crew", cw, sizeof cw); jget(buf, "since", sn, sizeof sn); jget(buf, "valid", va, sizeof va); if (strcmp(cw, g_crew) || strcmp(sn, g_since) || strcmp(va, g_valid)) { strcpy(g_crew, cw); strcpy(g_since, sn); strcpy(g_valid, va); g_dirty = 2; } }   /* 0.14; 0.16: valid = VALID TO (when the engine sends it) */
      jget(buf, "title", ti, sizeof ti); jget(buf, "room", room, sizeof room); jget(buf, "url", u, sizeof u); jget(buf, "line", l, sizeof l); jget(buf, "line2", l2, sizeof l2); jget(buf, "status", st, sizeof st);
      if (strcmp(room, g_room) || strcmp(u, g_url) || strcmp(l, g_line) || strcmp(l2, g_line2) || strcmp(st, g_status) || strcmp(ti, g_title) || g_err[0]) {
        strcpy(g_title, ti); strcpy(g_room, room); strcpy(g_url, u); strcpy(g_line, l); strcpy(g_line2, l2); strcpy(g_status, st); g_err[0] = 0; g_dirty = 2; }
    } else {
      char e[64]; if (!g_id[0]) snprintf(e, sizeof e, "this PS4's id not found - is the Round2 helper running?"); else if (!net_ok) snprintf(e, sizeof e, "network start 0x%08x", (unsigned)net_err); else snprintf(e, sizeof e, "https 0x%08x  http 0x%08x", (unsigned)n1, (unsigned)n);
      if (strcmp(g_status, "off") || strcmp(e, g_err)) { strcpy(g_status, "off"); strcpy(g_line, "Cannot reach Round2 right now - trying again..."); g_line2[0] = 0; strcpy(g_err, e); g_dirty = 2; }
    }
    pthread_mutex_unlock(&mx);
    /* 0.14: the words + timings (design/tile.json via the engine) – when the tile opens, then every 10 min */
    { static double cfg_at = -1e9; double tn = sceKernelGetProcessTime() / 1e6; static char cb[4096];
      if (net_ok && g_id[0] && tn - cfg_at > 600) { snprintf(url, sizeof url, "https://" ENGINE_HOST "/ps4/tilecfg?id=%s", g_id); int m = http_get(url, cb, sizeof cb);
        if (m <= 0) { snprintf(url, sizeof url, "http://" ENGINE_HOST "/ps4/tilecfg?id=%s", g_id); m = http_get(url, cb, sizeof cb); }
        if (m > 0 && strstr(cb, "\"s_title\"")) { TCfg c; cfg_default(&c); char v[128];
          #define CG(k, dst) { jget(cb, k, v, sizeof v); if (v[0] || !strcmp(k, "s_foot") || !strncmp(k, "c", 1)) snprintf(dst, sizeof dst, "%s", v); }
          CG("s_kick", c.s_kick) CG("s_title", c.s_title) CG("s_sub", c.s_sub) CG("s_foot", c.s_foot) CG("s_qr1", c.s_qr1) CG("s_qr2", c.s_qr2) CG("s_ps", c.s_ps) CG("s_url", c.s_url)
          CG("c1", c.c[0]) CG("c2", c.c[1]) CG("c3", c.c[2]) CG("c1n", c.cn[0]) CG("c2n", c.cn[1]) CG("c3n", c.cn[2])
          CG("w_kick", c.w_kick) CG("w_title", c.w_title) CG("w_sub", c.w_sub) CG("w_ready", c.w_ready) CG("w_ready2", c.w_ready2)
          CG("n_title", c.n_title) CG("n1", c.n[0]) CG("n2", c.n[1]) CG("n3", c.n[2]) CG("n4", c.n[3]) CG("n5", c.n[4])
          #undef CG
          jget(cb, "t_burst", v, sizeof v); if (v[0]) { float f = (float)atof(v); if (f >= 0.5f && f <= 5) c.t_burst = f; }
          jget(cb, "t_next", v, sizeof v); if (v[0]) { float f = (float)atof(v); if (f >= 3 && f <= 20) c.t_next = f; }
          pthread_mutex_lock(&mx); g_cfg = c; g_dirty = 2; pthread_mutex_unlock(&mx); cfg_at = tn; }
        else cfg_at = tn - 540; } }   /* failed: try again in a minute */
    sceKernelSleep(strcmp(g_status, "loading") && strcmp(g_status, "reset") ? 20 : 3);   /* faster while something is moving (0.08: 3 s) */
  }
  return 0;
}

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
static double now_s(void) { return sceKernelGetProcessTime() / 1e6; }
static FT_Face mload(const char *f, int px, void *ctx) { char p[160]; snprintf(p, sizeof p, "/app0/assets/fonts/%s", f); FT_Face fc = NULL; if (!((Scene2D *)ctx)->InitFont(&fc, p, px)) return NULL; return fc; }
int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  auto scene = new Scene2D(W, H, 4);
  if (!scene->Init(0xC000000, 2)) for (;;) sceKernelSleep(1);
  Fonts F; memset(&F, 0, sizeof F); fonts_load(F, mload, scene);   /* 0.16: the mockup's sizes */

  find_id();
  cfg_default(&g_cfg);
  net_init();
  pthread_t th; pthread_create(&th, NULL, poller, NULL);

  uint32_t *BASE = (uint32_t *)malloc(W * H * 4), *BG0 = (uint32_t *)malloc(W * H * 4);
  int frame = 0, busy = 0, done = 0, total = 0; char st[24] = "wait"; Color AC = GOLD; float shown = 0;
  char pkk[8] = "", pnn[8] = "", pnw[32] = "";
  int seen_loading = 0, howto = 0, seen_bar = 0, signedin = 0; double ready_t0 = 0, done_since = 0;   /* 0.08; 0.11: seen_bar, done_since */
  TRows B; memset(&B, 0, sizeof B); int was_busy = 0;
  TScr S; memset(&S, 0, sizeof S); TCfg C; cfg_default(&C);
  /* 0.16: which screen is up (0 scan, 1 loading, 2 ready, 3 what happens next), when it opened (its intro), whether BASE holds its still picture */
  int mode = -1, base_ok = 0; double mode_t0 = 0; Color bgAC = { 0, 0, 1 };
  for (;;) {
    if (frame % 15 == 0 && live_read()) g_dirty = 2;   /* 0.13 */
    if (g_dirty > 0 && !howto) {   /* 0.08: the instruction screen stays until the tile closes */
      char room[32], url[200], line[200], line2[200], err[64], ti[80], co[8], rn[32], crew[48], since[8], valid[8];
      pthread_mutex_lock(&mx); strcpy(ti, g_title); strcpy(room, g_room); strcpy(url, g_url); strcpy(line, g_line); strcpy(line2, g_line2); strcpy(st, g_status); strcpy(err, g_err);
      strcpy(co, g_color); strcpy(rn, g_rname); done = atoi(g_done); total = atoi(g_total); strcpy(pkk, g_pk); strcpy(pnn, g_pn); strcpy(pnw, g_pnow); strcpy(crew, g_crew); strcpy(since, g_since); strcpy(valid, g_valid); C = g_cfg; g_dirty--;
      pthread_mutex_unlock(&mx);
      if (lv_ok) { strcpy(st, lv_state == 'l' ? "loading" : "done"); }   /* 0.13: the helper on this PS4 knows best */
      AC = hexc(co, GOLD); busy = !strcmp(st, "loading") || !strcmp(st, "reset");
      { char bd[640]; pthread_mutex_lock(&mx); strcpy(bd, g_board); pthread_mutex_unlock(&mx); if (lv_ok) strcpy(bd, lv_board); B.bn = 0; char *p = bd;   /* 0.12 */
        while (*p && B.bn < 5) { char *e = strchr(p, '|'); if (e) *e = 0; char *f[4] = { p, (char *)"", (char *)"", (char *)"" }; int k = 1;
          for (char *q = p; *q && k < 4; q++) if (*q == '~') { *q = 0; f[k++] = q + 1; }
          int n = B.bn; B.bst[n] = f[0][0]; snprintf(B.bcur[n], 32, "%s", f[1]); snprintf(B.btgt[n], 32, "%s", f[2][0] ? f[2] : f[1]); snprintf(B.bwho[n], 32, "%s", f[3]); B.bn++;
          if (!e) break; p = e + 1; }
        if (busy && !was_busy) { memset(B.lit, 0, sizeof B.lit); B.last_lit = 0; } was_busy = busy; }
      if (lv_ok && B.bn > 0 && lv_state != 'l' && (!ti[0] || strncmp(ti, "Sorry", 5))) { ti[0] = 0; line[0] = 0; line2[0] = 0; }   /* 0.13 */
      if (busy) { seen_loading = 1; if (total > 0) seen_bar = 1; if (ready_t0 && !howto) ready_t0 = 0; done_since = 0; }   /* 0.11: back to loading = not the end */
      { int fin = !strcmp(st, "done") || (!strcmp(st, "off") && !strncmp(ti, "Sorry", 5));
        if (fin && seen_loading && !done_since) done_since = now_s();
        if (fin && seen_loading && !ready_t0 && (seen_bar || now_s() - done_since >= (lv_ok ? 3 : 10))) ready_t0 = now_s(); }   /* 0.08/0.10/0.11; 0.17: 3 s when the helper's file says done */
      /* the screen's words. Scan screen: only a status line when the room isn't simply ready (reset, can't reach Round2,
         "One moment – this PS4 is finishing a job", signed in for this session). The version + id only show with an error. */
      signedin = (B.bn > 0 && (busy || !strcmp(st, "done"))) || (!strcmp(st, "loading") && crew[0]);
      strcpy(S.room, room); strcpy(S.rname, rn); strcpy(S.url, url); strcpy(S.crew, crew); strcpy(S.since, since); strcpy(S.valid, valid); strcpy(S.st, st); S.AC = AC; S.busy = busy; S.done = !strcmp(st, "done");
      if (!strcmp(st, "ready")) { S.title[0] = 0; if (!strncmp(line2, "One moment", 10)) { strcpy(S.line, line2); strcpy(S.st, "note"); } else S.line[0] = 0; S.line2[0] = 0; }
      else if (signedin) { S.title[0] = 0; if (!strncmp(ti, "Sorry", 5)) strcpy(S.line, ti); else S.line[0] = 0; S.line2[0] = 0; }   /* 0.16: the rows say who is loading */
      else { strcpy(S.title, ti); strcpy(S.line, line); strcpy(S.line2, line2); }
      if (err[0]) snprintf(S.err, sizeof S.err, "%s   (tile " TILE_V ", id %s)", err, g_idsrc); else S.err[0] = 0;
      base_ok = 0;
    }
    /* which screen, and its background (once per room colour) */
    { int nm = howto ? 3 : (ready_t0 || (signedin && S.done)) ? 2 : signedin ? 1 : 0;
      if (nm != mode) { mode = nm; mode_t0 = now_s(); base_ok = 0; }
      if (memcmp(&AC, &bgAC, sizeof AC)) { bgAC = AC; if (BG0) bg_v2(BG0, AC); base_ok = 0; } }
    /* each frame: the screen (its intro, or the still picture from BASE), then the moving parts */
    double nowt = now_s(), it = nowt - mode_t0, intro = mode == 0 ? 1.6 : mode == 1 ? 1.2 : mode == 2 ? 1.0 : 0.05; FB = scene->Buf();
    if (it < intro || !BASE) { if (BG0) memcpy(FB, BG0, W * H * 4);
      if (mode == 0) scan_still(F, C, S, it); else if (mode == 1) load_still(F, C, S, it); else if (mode == 2) ready_still(F, C, S, it); else next_still(F, C, S); base_ok = 0; }
    else { if (!base_ok) { FB = BASE; if (BG0) memcpy(BASE, BG0, W * H * 4);
        if (mode == 0) scan_still(F, C, S, 99); else if (mode == 1) load_still(F, C, S, 99); else if (mode == 2) ready_still(F, C, S, 99); else next_still(F, C, S);
        base_ok = 1; FB = scene->Buf(); }
      memcpy(FB, BASE, W * H * 4); }
    GA = 255;
    if (mode == 0) scan_live(S, nowt, ease((float)((it - 0.4) / 0.8)));
    else if (mode == 1) {
      if (it >= intro) card_sweep(KX, KY, it);
      int bnt = 0, bnd = 0, bli = 0; for (int i = 0; i < B.bn; i++) { char c = B.bst[i]; if (c == 'w' || c == 'l' || c == 'd' || c == 'f') bnt++; if (c == 'd' || c == 'f') bnd++; if (c == 'l') bli = 1; }
      if (B.bn > 0 && bnt > 0) {   /* 0.13 (owner: 1 of 3 profiles in = the bar a third full): the bar is the whole load, player by player */
        int pd = lv_ok ? lv_pd : done, pt = lv_ok ? lv_pt : total; static int lastk = -1; static double since2 = 0; int key = bnd * 100 + pd; if (key != lastk) { lastk = key; since2 = now_s(); }
        double tt = now_s() - since2; float inside = bli && pt > 0 ? ((float)pd + (float)(0.9 * (1.0 - 1.0 / (1.0 + tt / 20.0)))) / pt : (bli ? (float)(0.5 * (1.0 - 1.0 / (1.0 + tt / 15.0))) : 0);
        if (inside > 0.98f) inside = 0.98f; float want = ((float)bnd + inside) / bnt; shown += (want - shown) * 0.06f; if (shown > want) shown = want;
        S.pmode = 1; S.prog = shown; snprintf(S.plab, sizeof S.plab, "%d of %d profile%s ready", bnd, bnt, bnt == 1 ? "" : "s"); }
      else if (total > 0) { static int lastdone = -1; static double since = 0; if (done != lastdone) { lastdone = done; since = now_s(); }
        double t = now_s() - since; float creep = done < total ? (float)(0.9 * (1.0 - 1.0 / (1.0 + t / 20.0))) : 0;   /* 0.09: inside a game */
        float want = ((float)done + creep) / total; shown += (want - shown) * 0.06f; if (shown > want) shown = want;
        S.pmode = 1; S.prog = shown;
        if (pnn[0] && atoi(pnn) > 1) snprintf(S.plab, sizeof S.plab, "%s (player %s of %s)  -  %d of %d games", pnw, pkk, pnn, done, total); else snprintf(S.plab, sizeof S.plab, "%d of %d games", done, total); }
      else { S.pmode = 2; snprintf(S.plab, sizeof S.plab, "Getting ready"); }
      draw_rows(F, C, S, B, nowt, 389, 1);
      load_bar(F, S, B.bn > 0 ? 389 + B.bn * PRS - 12 + 22 : 399, frame);
    }
    else if (mode == 2) { ready_live(S, it); draw_rows(F, C, S, B, nowt, 400, 0); }
    if (!busy) shown = 0;   /* 0.13 */
    if (!ready_t0 && done_since && seen_loading && now_s() - done_since >= (lv_ok ? 3 : 10) && (!strcmp(st, "done") || !strcmp(st, "off"))) ready_t0 = now_s();   /* 0.11: nothing-to-load fallback; 0.17: 3 s with the helper's file */
    if (ready_t0) {   /* 0.08: ready, then "what happens next", then close – both times from design/tile.json */
      double e = now_s() - ready_t0, tb = C.t_burst, tn = C.t_next;
      if (e >= tb && !howto) howto = 1;   /* the next frame switches to screen 4 */
      if (mode == 3) next_live(F, C, S, it, tb + tn - e, tn);
      if (e >= tb + tn) { sceSystemServiceLoadExec("exit", NULL); exit(0); }
    }
    scene->SubmitFlip(frame); scene->FrameWait(frame); scene->FrameBufferSwap(); frame++;
  }
  return 0;
}
