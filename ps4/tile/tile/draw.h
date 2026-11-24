/* Round2 tile 0.14 – the screens (24 Nov 2026, design's membership pack: "new scan screen … new signed-in screen … room-colour
   glow, calm motion"). Everything that paints pixels lives here, so the desktop simulator (round2-tv-launcher ps4/tile/sim) draws
   exactly what the PS4 draws. Words and timings come from the engine's /ps4/tilecfg (design/tile.json) – see TCfg.
   Layout (1920x1080): words left (x 120-660), cards middle (690-1210), QR / profiles right (1290-1810). */
#ifndef R2_DRAW_H
#define R2_DRAW_H
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "logo.h"

#define W 1920
#define H 1080
static Color BG = { 6, 5, 10 }, CARD = { 20, 17, 31 }, INK = { 244, 241, 251 }, DIM = { 167, 159, 189 }, GOLD = { 245, 196, 0 },
             WHITE = { 255, 255, 255 }, BLACK = { 0, 0, 0 }, RED = { 255, 122, 122 }, GREEN = { 125, 255, 168 };

/* ---- drawing straight into a frame buffer (alpha-blended); GA = global strength for fades (0-255) ---- */
static uint32_t *FB; static int GA = 255;
static inline void blend(int x, int y, Color c, int a) {
  if (x < 0 || y < 0 || x >= W || y >= H) return; if (GA < 255) a = a * GA / 255; if (a <= 0) return;
  uint32_t *p = FB + y * W + x;
  if (a >= 255) { *p = 0xFF000000u | (c.r << 16) | (c.g << 8) | c.b; return; }
  uint32_t o = *p; int r = (o >> 16) & 255, g = (o >> 8) & 255, b = o & 255;
  r += (c.r - r) * a / 255; g += (c.g - g) * a / 255; b += (c.b - b) * a / 255;
  *p = 0xFF000000u | (r << 16) | (g << 8) | b;
}
static void rect(int x, int y, int w, int h, Color c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) blend(i, j, c, 255); }
static void recta(int x, int y, int w, int h, Color c, int a) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) blend(i, j, c, a); }
/* one line of text; y = baseline; sp = extra letter spacing; returns the x after it (draw = 0: just measure) */
static int text(FT_Face f, const char *s, int x, int y, Color c, int draw = 1, int sp = 0) {
  for (; *s; s++) {
    unsigned ch = (unsigned char)*s; if (ch >= 128) ch = '?';
    if (FT_Load_Glyph(f, FT_Get_Char_Index(f, ch), FT_LOAD_DEFAULT) || FT_Render_Glyph(f->glyph, ft_render_mode_normal)) continue;
    FT_GlyphSlot g = f->glyph; FT_Bitmap *b = &g->bitmap;
    if (draw) for (int r = 0; r < (int)b->rows; r++) for (int k = 0; k < (int)b->width; k++)
      blend(x + g->bitmap_left + k, y - g->bitmap_top + r, c, ((unsigned char *)b->buffer)[r * b->pitch + k]);
    x += (g->advance.x >> 6) + sp;
  }
  return x;
}
static int textw(FT_Face f, const char *s, int sp = 0) { return text(f, s, 0, 0, WHITE, 0, sp); }
/* word wrap to a pixel width; returns the y under the last line */
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
static Color mixc(Color a, Color b, float k) { if (k < 0) k = 0; if (k > 1) k = 1; Color c = { (uint8_t)(a.r + (b.r - a.r) * k), (uint8_t)(a.g + (b.g - a.g) * k), (uint8_t)(a.b + (b.b - a.b) * k) }; return c; }
static void ring(int cx, int cy, int r, int t, Color c, int a) {
  if (r <= 0 || a <= 0) return; int r2o = (r + t) * (r + t), r2i = r * r;
  for (int y = cy - r - t; y <= cy + r + t; y++) for (int x = cx - r - t; x <= cx + r + t; x++) { int d = (x - cx) * (x - cx) + (y - cy) * (y - cy); if (d >= r2i && d <= r2o) blend(x, y, c, a); } }
static void disc(int cx, int cy, int r, Color c, int a) { for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) { int d = x * x + y * y; if (d <= r * r) blend(cx + x, cy + y, c, d > (r - 1) * (r - 1) ? a / 2 : a); } }
static void frame_box(int x, int y, int w, int h, int t, Color c, int a) { for (int k = 0; k < t; k++) { for (int i = x - k; i < x + w + k; i++) { blend(i, y - k - 1, c, a); blend(i, y + h + k, c, a); } for (int j = y - k; j < y + h + k; j++) { blend(x - k - 1, j, c, a); blend(x + w + k, j, c, a); } } }
/* the Round2 logo, shrunk by div (1 = 640x163), colour c at strength a; sx = centre of a white shine band (logo pixels, -1 = none);
   fade = how far it sinks into the background first (0.09's breathing) */
static void logo_d(int x, int y, int div, Color c, int a, int sx = -9999, int fade = 0) {
  int w = LOGO_W / div, h = LOGO_H / div;
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) { int s = 0; for (int v = 0; v < div; v++) for (int u = 0; u < div; u++) s += LOGO_A[(j * div + v) * LOGO_W + i * div + u]; int la = s / (div * div); if (!la) continue;
    if (fade) blend(x + i, y + j, BG, la * fade / 255);
    blend(x + i, y + j, c, la * a / 255);
    if (sx > -9999) { int d = abs(i * div - sx + (j * div - LOGO_H / 2) / 3); if (d < 70) blend(x + i, y + j, WHITE, la * (70 - d) / 75); } } }
static float ease(float t) { if (t <= 0) return 0; if (t >= 1) return 1; return 1 - (1 - t) * (1 - t) * (1 - t); }   /* calm ease-out, no bounce */

/* ---- words + timings (engine /ps4/tilecfg = design/tile.json, defaults = the tile's own words) ---- */
struct TCfg { char s_kick[32], s_title[64], s_sub[120], s_foot[88], s_qr1[40], s_qr2[40], s_ps[56], s_url[36], c[3][16], cn[3][24],
  w_kick[32], w_title[64], w_sub[120], w_ready[64], w_ready2[96], n_title[48], n[5][104]; float t_burst, t_next; };
static void cfg_default(TCfg *c) {
  memset(c, 0, sizeof *c);
  strcpy(c->s_kick, "ROUND2 MEMBERS"); strcpy(c->s_title, "Pick up where you left off."); strcpy(c->s_sub, "Save your game here and carry on from any Round2 room.");
  strcpy(c->s_qr1, "Members scan to log in"); strcpy(c->s_qr2, "New guests scan to join"); strcpy(c->s_ps, "Not saving today? Press PS to play."); strcpy(c->s_url, "book.round2.sg/m");
  strcpy(c->c[0], "ROUND2"); strcpy(c->c[1], "ROUND2"); strcpy(c->c[2], "ROUND2 CREW");
  strcpy(c->w_kick, "ROUND2 CREW"); strcpy(c->w_title, "Welcome back, {crew}."); strcpy(c->w_sub, "Your saves are loading onto this PS4. Please don't start a game yet.");
  strcpy(c->w_ready, "{crew} is all set."); strcpy(c->w_ready2, "Press PS on your controller and pick your profile.");
  strcpy(c->n_title, "What happens next"); strcpy(c->n[0], "1.  This screen closes by itself."); strcpy(c->n[1], "2.  Your TV shows \"Press PS button\".");
  strcpy(c->n[2], "3.  Press the PS button on your controller."); strcpy(c->n[3], "4.  Pick your profile - it has your name and picture.");
  strcpy(c->n[4], "5.  More players? Each one presses PS on their own controller and picks theirs."); c->t_burst = 1.5f; c->t_next = 6.f;
}
static void put_crew(char *out, int n, const char *tpl, const char *crew) {   /* "{crew}" → the crew's name */
  const char *p = strstr(tpl, "{crew}"); if (!p) { snprintf(out, n, "%s", tpl); return; }
  snprintf(out, n, "%.*s%s%s", (int)(p - tpl), tpl, crew[0] ? crew : "your crew", p + 6);
}

/* ---- the background: room-colour glow(s), a fine dot texture inside the glow, darker edges ---- */
struct Glow { int x, y, r; float k; };
static void bg_compose(uint32_t *dst, Color AC, const Glow *gl, int ng) {
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    float g = 0; for (int i = 0; i < ng; i++) { float dx = (float)(x - gl[i].x), dy = (float)(y - gl[i].y), d = (dx * dx + dy * dy) / ((float)gl[i].r * gl[i].r); if (d < 1) { float v = (1 - d) * (1 - d) * gl[i].k; if (v > g) g = v; } }
    float ex = (x - W / 2.f) / (W / 2.f), ey = (y - H / 2.f) / (H / 2.f), e = ex * ex * 0.6f + ey * ey * 0.8f, vig = e > 0.35f ? 1 - (e - 0.35f) * 0.55f : 1; if (vig < 0.45f) vig = 0.45f;
    float r = (BG.r + (AC.r - BG.r) * g) * vig, gg = (BG.g + (AC.g - BG.g) * g) * vig, b = (BG.b + (AC.b - BG.b) * g) * vig;
    if (g > 0.02f && x % 9 < 2 && y % 9 < 2) { float a = g * 0.30f; r += (255 - r) * a; gg += (255 - gg) * a; b += (255 - b) * a; }
    dst[y * W + x] = 0xFF000000u | ((int)r << 16) | ((int)gg << 8) | (int)b; }
}

/* ---- a membership card: muted matte (0 charcoal, 1 bronze-gold, 2 near-black), dot grid fading in from one corner, thin inner
   edge, outline chip, small Round2 logo. top/note = the top strip; name/since = the crew (front card only). dots = how far the
   dot grid has lit up, left to right (0-1); sweep = x of one light band across the card (-1 = none) ---- */
static const Color CARD_A[3] = { { 59, 59, 65 }, { 62, 52, 33 }, { 29, 29, 33 } }, CARD_B[3] = { { 42, 42, 47 }, { 42, 35, 22 }, { 14, 14, 16 } },
                   CARD_T[3] = { { 220, 219, 225 }, { 220, 191, 124 }, { 212, 212, 218 } };
static void card(FT_Face fTop, FT_Face fName, FT_Face fSm, int x, int y, int w, int h, int kind, const char *top, const char *note, const char *name, const char *since, float dots, int sweep) {
  int R = h / 13; Color A = CARD_A[kind], B = CARD_B[kind], TX = CARD_T[kind];
  for (int k = 0; k < 3; k++) recta(x + 6 + k * 3, y + 12 + k * 4, w - k * 6, h, BLACK, 34);   /* soft shadow (cheap) */
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    int cx = i < R ? R - i : i >= w - R ? i - (w - R - 1) : 0, cy = j < R ? R - j : j >= h - R ? j - (h - R - 1) : 0; if (cx && cy && cx * cx + cy * cy > R * R) continue;
    float t = (0.34f * i / w + 0.94f * j / h) / 0.77f; Color c = mixc(A, B, t / 0.6f); blend(x + i, y + j, c, 255);
    float m = (0.5f * i / w + 0.87f * j / h - 0.35f) / 0.85f; if (m > 0 && (i % 9) < 2 && (j % 9) < 2 && i < w * dots) blend(x + i, y + j, WHITE, (int)(26 * (m > 1 ? 1 : m)));
    int ex = i - 6, ey = j - 6, iw = w - 12, ih = h - 12, eR = R - 5;   /* inner edge */
    if ((ex == 0 || ex == iw - 1 || ey == 0 || ey == ih - 1) && ex >= 0 && ey >= 0 && ex < iw && ey < ih) { int qx = ex < eR ? eR - ex : ex >= iw - eR ? ex - (iw - eR - 1) : 0, qy = ey < eR ? eR - ey : ey >= ih - eR ? ey - (ih - eR - 1) : 0; if (!(qx && qy)) blend(x + i, y + j, WHITE, 18); }
    if (sweep > -9999) { int d = abs(i - sweep + (j - h / 2) / 3); if (d < 60) blend(x + i, y + j, WHITE, (60 - d) * 46 / 60); } }
  for (int i = 0; i < w; i++) { if (i >= R && i < w - R) { blend(x + i, y, WHITE, 40); blend(x + i, y + h - 1, BLACK, 60); } }   /* border */
  int pad = w / 16, sz = h / 11;
  if (top && top[0]) text(fTop, top, x + pad, y + pad + sz, TX, 1, 5);
  if (note && note[0]) { int nw = textw(fTop, note); text(fTop, note, x + w - pad - nw, y + pad + sz, mixc(TX, B, 0.2f)); }
  if (name) {   /* front card: chip, crew name, since, logo */
    int chx = x + pad, chy = y + h * 42 / 100, cw2 = w / 15, ch2 = cw2 * 3 / 4; frame_box(chx, chy, cw2, ch2, 2, TX, 110);
    int ny = y + h - pad - sz * 2 - 6; if (name[0]) wrap(fName, name, x + pad, ny, w - pad * 2 - 40, sz + 10, TX);
    if (since && since[0]) { char sv[32]; snprintf(sv, sizeof sv, "CREW SINCE %s", since); text(fSm, sv, x + pad, y + h - pad, mixc(TX, B, 0.3f), 1, 3); }
    logo_d(x + w - pad - LOGO_W / 8, y + pad + 2, 8, TX, 205);
  }
}
/* QR code with room-colour corner brackets (snap = 0-1, brackets fly in from 40 px out) */
static void draw_qr(const char *t, int x, int y, int size) {
  static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
  if (!t[0] || !qrcodegen_encodeText(t, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return;
  int q = qrcodegen_getSize(qr), cell = size / (q + 8), off = (size - cell * q) / 2;
  rect(x, y, size, size, WHITE);
  for (int r = 0; r < q; r++) for (int c = 0; c < q; c++) if (qrcodegen_getModule(qr, c, r)) rect(x + off + c * cell, y + off + r * cell, cell, cell, BLACK);
}
static void brackets(int x, int y, int s, Color c, int a, float snap) {
  int o = 18 + (int)(40 * (1 - ease(snap))), L = 64, T = 8;
  int X0 = x - o, Y0 = y - o, X1 = x + s + o, Y1 = y + s + o;
  recta(X0, Y0, L, T, c, a); recta(X0, Y0, T, L, c, a); recta(X1 - L, Y0, L, T, c, a); recta(X1 - T, Y0, T, L, c, a);
  recta(X0, Y1 - T, L, T, c, a); recta(X0, Y1 - L, T, L, c, a); recta(X1 - L, Y1 - T, L, T, c, a); recta(X1 - T, Y1 - L, T, L, c, a);
}

/* ---- screen state (what the engine + the helper said) ---- */
struct Fonts { FT_Face big, mid, small, tiny, pix, pixs; };
struct TScr { char room[32], rname[32], url[200], crew[48], since[8], title[80], line[200], line2[200], err[96], st[24]; Color AC; int busy, done; };

/* positions */
static const int LX = 120, LW = 540, QX = 1370, QY = 250, QS = 400, SCX = 700, SCY = 300, SCW = 440, SCH = 277, SDX = 14, SDY = 58;
static const int WCX = 690, WCY = 300, WCW = 520, WCH = 328;   /* the crew card on the signed-in screen */

/* the scan screen; t = seconds since it opened (intro ~1.6 s: top line draws → words rise → cards deal back to front → QR corners
   snap in → one sweep across the front card); t >= 3 = still picture */
static void draw_scan(const Fonts &F, const TCfg &C, const TScr &S, double t) {
  Color AC = S.AC; float k;
  k = ease((float)(t / 0.45)); recta(0, 0, (int)(W * k), 6, AC, 255);
  GA = 255; logo_d(LX, 96, 2, WHITE, 255);
  /* words rise, 0.08 s apart */
  const double w0 = 0.30; int y;
  k = ease((float)((t - w0) / 0.4)); GA = (int)(255 * k); { int dy = (int)(14 * (1 - k)); int x = text(F.tiny, C.s_kick, LX, 300 + dy, DIM, 1, 4);
    if (S.rname[0] || S.room[0]) { char up[32]; snprintf(up, sizeof up, "%s", S.rname[0] ? S.rname : S.room); for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32; text(F.tiny, "-", x + 10, 300 + dy, DIM); text(F.tiny, up, x + 34, 300 + dy, AC, 1, 4); } }
  k = ease((float)((t - w0 - 0.08) / 0.4)); GA = (int)(255 * k); y = wrap(F.big, C.s_title, LX, 400 + (int)(14 * (1 - k)), LW, 80, INK);
  k = ease((float)((t - w0 - 0.16) / 0.4)); GA = (int)(255 * k); y = wrap(F.small, C.s_sub, LX, y + 22 + (int)(14 * (1 - k)), LW, 46, mixc(DIM, INK, 0.45f));
  /* a status line when the room isn't simply ready (being reset / can't reach Round2 / signed in for this session) */
  if (S.title[0] || (strcmp(S.st, "ready") && S.line[0])) { k = ease((float)((t - w0 - 0.24) / 0.4)); GA = (int)(255 * k);
    Color sc = !strcmp(S.st, "off") ? RED : !strcmp(S.st, "done") ? GREEN : AC; y += 26;
    if (S.title[0]) y = wrap(F.small, S.title, LX, y + 10, LW, 46, sc);
    y = wrap(F.tiny, S.line, LX, y + 8, LW, 34, S.title[0] ? INK : sc); if (S.line2[0]) wrap(F.tiny, S.line2, LX, y + 4, LW, 34, DIM); }
  if (C.s_foot[0]) { k = ease((float)((t - w0 - 0.32) / 0.4)); GA = (int)(255 * k); text(F.tiny, C.s_foot, LX, 930, DIM); }
  /* the card stack deals in, back to front */
  for (int i = 0; i < 3; i++) { double d0 = 0.5 + i * 0.12; k = ease((float)((t - d0) / 0.5)); if (k <= 0) continue; GA = (int)(255 * k);
    int x = SCX + i * SDX, yy = SCY + i * SDY + (int)(60 * (1 - k)), front = i == 2;
    double sw = t - 1.3; int sweep = front && sw > 0 && sw < 0.8 ? (int)(-80 + sw / 0.8 * (SCW + 160)) : -9999;
    card(F.tiny, F.pixs, F.tiny, x, yy, SCW, SCH, i == 0 ? 0 : i == 1 ? 1 : 2, C.c[i], C.cn[i], front ? "" : NULL, NULL, 1.f, sweep); }
  /* QR with corners */
  k = ease((float)((t - 0.9) / 0.3)); GA = (int)(255 * k);
  if (S.url[0]) draw_qr(S.url, QX, QY, QS); else { recta(QX, QY, QS, QS, CARD, 255); text(F.small, "QR code appears when", QX + 30, QY + 190, DIM); text(F.small, "Round2 answers", QX + 70, QY + 240, DIM); }
  GA = 255; if (t > 1.05 && t < 2.0) brackets(QX, QY, QS, AC, 255, (float)((t - 1.05) / 0.22));   /* after the intro the tile draws them each frame (they breathe) */
  k = ease((float)((t - 1.0) / 0.4)); GA = (int)(255 * k);
  { int w1 = textw(F.small, C.s_qr1); text(F.small, C.s_qr1, QX + QS / 2 - w1 / 2, QY + QS + 96, INK); int w2 = textw(F.small, C.s_qr2); text(F.small, C.s_qr2, QX + QS / 2 - w2 / 2, QY + QS + 142, DIM); }
  k = ease((float)((t - 1.1) / 0.4)); GA = (int)(255 * k);
  text(F.small, C.s_ps, LX, 1020, DIM); { int w3 = textw(F.tiny, C.s_url); text(F.tiny, C.s_url, QX + QS / 2 - w3 / 2, 1020, DIM); }
  GA = 255;
  if (S.err[0]) text(F.tiny, S.err, LX, 1066, DIM);
}
/* the signed-in screen ("Welcome back, <crew>"); t = seconds since it opened (intro ~2 s: the crew card rises → its dot grid lights
   up left to right → one sweep → "Welcome back" words). The profiles list + bar are drawn each frame by the tile itself. */
static void draw_signed(const Fonts &F, const TCfg &C, const TScr &S, double t) {
  Color AC = S.AC; float k; char b[200];
  recta(0, 0, W, 6, AC, 255); logo_d(LX, 96, 2, WHITE, 255);
  k = ease((float)(t / 0.7)); GA = (int)(255 * k);
  { double sw = t - 1.2; int sweep = sw > 0 && sw < 0.8 ? (int)(-80 + sw / 0.8 * (WCW + 160)) : -9999; float dots = t < 0.6 ? 0 : (float)((t - 0.6) / 0.6);
    card(F.tiny, F.pixs, F.tiny, WCX, WCY + (int)(40 * (1 - k)), WCW, WCH, 0, "ROUND2 CREW", "", S.crew[0] ? S.crew : "YOUR CREW", S.since, dots > 1 ? 1 : dots, sweep); }
  k = ease((float)((t - 1.5) / 0.5)); GA = (int)(255 * k); int dy = (int)(14 * (1 - k)), y;
  text(F.tiny, C.w_kick, LX, 300 + dy, AC, 1, 4);
  put_crew(b, sizeof b, S.done ? C.w_ready : C.w_title, S.crew); y = wrap(F.big, b, LX, 400 + dy, LW, 80, S.done ? GREEN : INK);
  put_crew(b, sizeof b, S.done ? C.w_ready2 : C.w_sub, S.crew); y = wrap(F.small, b, LX, y + 22, LW, 46, mixc(DIM, INK, 0.45f));
  if (S.line[0] && !S.done) wrap(F.tiny, S.line, LX, y + 22, LW, 34, AC);
  GA = 255; if (S.err[0]) text(F.tiny, S.err, LX, 1066, DIM);
}
/* ---- the profiles list on the signed-in screen (0.12 cards, now a list on the right): main + Player 1-4 start grey, each turns the
   room colour in order as it's ready, a Player row changes into the member's name, the one being worked on pulses ---- */
static const int CX0 = 1290, CY0 = 230, CW = 520, CH = 104, CG = 12;
struct TRows { int bn; char bst[5], bcur[5][32], btgt[5][32], bwho[5][32]; int lit[5]; double lit_t[5], last_lit; };
static void draw_rows(const Fonts &F, Color AC, TRows &B, double t) {
  Color GREY = { 58, 54, 72 }; GA = 255;
  text(F.pixs, "PROFILES", CX0, CY0 - 30, AC);
  for (int i = 0; i < B.bn; i++) { if (B.lit[i]) continue; char c = B.bst[i]; if ((c == 'd' || c == 'f' || c == 'u') && t - B.last_lit >= 0.45) { B.lit[i] = 1; B.lit_t[i] = t; B.last_lit = t; } break; }
  for (int i = 0; i < B.bn; i++) {
    int y = CY0 + i * (CH + CG), L = B.lit[i], active = !L && (B.bst[i] == 'c' || B.bst[i] == 'l'); double since = L ? t - B.lit_t[i] : 0;
    recta(CX0, y, CW, CH, CARD, 235); rect(CX0, y, 6, CH, L ? AC : GREY);
    if (L && since < 0.6) recta(CX0, y, CW, CH, AC, (int)(110 * (1 - since / 0.6)));
    if (active) { double ph = fmod(t, 1.6) / 1.6; int sx = CX0 - 120 + (int)(ph * (CW + 240));
      for (int xx = sx - 70; xx < sx + 70; xx++) { if (xx < CX0 || xx >= CX0 + CW) continue; int a = 34 - abs(xx - sx) * 34 / 70; for (int yy = y; yy < y + CH; yy++) blend(xx, yy, WHITE, a); }
      int pa = 110 + (int)(100 * (ph < 0.5 ? ph * 2 : 2 - ph * 2)); frame_box(CX0, y, CW, CH, 3, AC, pa); }
    int cx = CX0 + 62, cy = y + CH / 2, r = 34; const char *nm = L ? B.btgt[i] : B.bcur[i];
    disc(cx, cy, r, L ? AC : GREY, 255);
    if (active) { double ph = fmod(t, 1.2) / 1.2; ring(cx, cy, r + 4 + (int)(ph * 22), 3, AC, (int)(190 * (1 - ph))); }
    if (L && since < 0.9) ring(cx, cy, r + (int)(since * 120), 5, AC, (int)(220 * (1 - since / 0.9)));
    { char in[2] = { (char)(nm[0] >= 'a' && nm[0] <= 'z' ? nm[0] - 32 : nm[0] ? nm[0] : '?'), 0 }; int w2 = textw(F.small, in); text(F.small, in, cx - w2 / 2, cy + 12, L ? BG : DIM); }
    int nx = CX0 + 118, ny = y + 46;
    if (L && strcmp(B.bcur[i], B.btgt[i]) && since < 0.55) { float k = (float)(since / 0.55);
      text(F.small, B.bcur[i], nx, ny - (int)(k * 26), mixc(DIM, CARD, k)); text(F.small, B.btgt[i], nx, ny + 26 - (int)(k * 26), mixc(CARD, INK, k)); }
    else text(F.small, nm, nx, ny, L ? INK : DIM);
    char ln[80]; Color lc = DIM; char c = B.bst[i];
    if (L) { if (c == 'f') { snprintf(ln, sizeof ln, "Ready - some games failed, staff will check"); lc = RED; } else { snprintf(ln, sizeof ln, "Ready"); lc = GREEN; } }
    else if (c == 'c') { snprintf(ln, sizeof ln, "Clearing for the next player..."); lc = AC; }
    else if (c == 'l') { snprintf(ln, sizeof ln, "Loading %s's saves...", B.bwho[i][0] ? B.bwho[i] : "the"); lc = AC; }
    else if (c == 'w') snprintf(ln, sizeof ln, "Next - %s", B.bwho[i][0] ? B.bwho[i] : "a player");
    else snprintf(ln, sizeof ln, "Waiting");
    if (active) { int n = (int)(t * 2.5) % 4; size_t l = strlen(ln); while (l && ln[l - 1] == '.') ln[--l] = 0; for (int d = 0; d < n && l < sizeof ln - 1; d++) ln[l++] = '.'; ln[l] = 0; }
    text(F.tiny, ln, nx, y + 84, lc);
    if (L) { int kx = CX0 + CW - 54, ky = y + CH / 2 - 8; float g = since < 0.35 ? (float)(since / 0.35) : 1.f;
      for (int s2 = 0; s2 < (int)(14 * g); s2++) rect(kx + s2, ky + s2, 5, 5, c == 'f' ? RED : GREEN);
      if (g >= 1) for (int s2 = 0; s2 < 26; s2++) rect(kx + 13 + s2, ky + 13 - s2, 5, 5, c == 'f' ? RED : GREEN); }
  }
}
#endif
