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
  strcpy(c->s_qr1, "Members scan to log in"); strcpy(c->s_qr2, "New guests scan to join"); strcpy(c->s_ps, "Not saving today? Press [PS] to play."); strcpy(c->s_url, "book.round2.sg/m");
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

/* ---- a membership card (Karen's mockups 24 Nov): muted matte (0 charcoal, 1 bronze-gold, 2 near-black), dot grid fading in from
   one corner, thin inner edge, outline chip, small Round2 logo. mode 0 = a card at the back of the stack (top strip only), 1 = the
   front card of the scan stack (chip, logo bottom-left, "MEMBER" bottom-right), 2 = the crew card (logo top-right, chip, crew
   name + CREW SINCE bottom-left). top/note = the top strip. dots = how far the dot grid has lit up, left to right (0-1);
   sweep = x of one light band across the card (-9999 = none) ---- */
struct Fonts { FT_Face big, mid, small, tiny, pix, pixs, body; };
static const Color CARD_A[3] = { { 59, 59, 65 }, { 62, 52, 33 }, { 29, 29, 33 } }, CARD_B[3] = { { 42, 42, 47 }, { 42, 35, 22 }, { 14, 14, 16 } },
                   CARD_T[3] = { { 220, 219, 225 }, { 220, 191, 124 }, { 212, 212, 218 } }, CARD_E[3] = { { 120, 120, 128 }, { 150, 122, 70 }, { 90, 90, 98 } };
static void card(const Fonts &F, int x, int y, int w, int h, int kind, int mode, const char *top, const char *note, const char *name, const char *since, float dots, int sweep) {
  int R = h / 14; Color A = CARD_A[kind], B = CARD_B[kind], TX = CARD_T[kind];
  for (int k = 0; k < 3; k++) recta(x + 4 + k * 3, y + 12 + k * 5, w - k * 4, h, BLACK, 40);   /* soft shadow (cheap) */
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    int cx = i < R ? R - i : i >= w - R ? i - (w - R - 1) : 0, cy = j < R ? R - j : j >= h - R ? j - (h - R - 1) : 0; if (cx && cy && cx * cx + cy * cy > R * R) continue;
    float t = (0.34f * i / w + 0.94f * j / h) / 0.77f; blend(x + i, y + j, mixc(A, B, t / 0.6f), 255);
    float m = (0.5f * i / w + 0.87f * j / h - 0.35f) / 0.85f; if (m > 0 && (i % 9) < 2 && (j % 9) < 2 && i < w * dots) blend(x + i, y + j, WHITE, (int)(26 * (m > 1 ? 1 : m)));
    int ex = i - 6, ey = j - 6, iw = w - 12, ih = h - 12, eR = R - 5;   /* thin inner edge */
    if ((ex == 0 || ex == iw - 1 || ey == 0 || ey == ih - 1) && ex >= 0 && ey >= 0 && ex < iw && ey < ih) { int qx = ex < eR ? eR - ex : ex >= iw - eR ? ex - (iw - eR - 1) : 0, qy = ey < eR ? eR - ey : ey >= ih - eR ? ey - (ih - eR - 1) : 0; if (!(qx && qy)) blend(x + i, y + j, WHITE, 16); }
    int ox = cx ? cx : 0, oy = cy ? cy : 0; if (i == 0 || j == 0 || i == w - 1 || j == h - 1 || (cx && cy && cx * cx + cy * cy > (R - 1) * (R - 1))) blend(x + i, y + j, CARD_E[kind], 150); (void)ox; (void)oy;   /* outline */
    if (sweep > -9999) { int d = abs(i - sweep + (j - h / 2) / 3); if (d < 60) blend(x + i, y + j, WHITE, (60 - d) * 46 / 60); } }
  int pad = w * 6 / 100, ty = y + pad + 22;
  if (top && top[0]) text(F.tiny, top, x + pad, ty, TX, 1, 7);
  if (note && note[0]) { int nw = textw(F.tiny, note); text(F.tiny, note, x + w - pad - nw, ty, mixc(TX, B, 0.15f)); }
  if (mode == 0) return;
  int cw2 = w / 14, ch2 = cw2 * 3 / 4, chy = y + h * (mode == 1 ? 58 : 38) / 100; frame_box(x + pad, chy, cw2, ch2, 2, TX, 110);
  if (mode == 1) { logo_d(x + pad, y + h - pad - LOGO_H / 7, 7, TX, 200); int mw = textw(F.tiny, "MEMBER", 5); text(F.tiny, "MEMBER", x + w - pad - mw, y + h - pad - 2, mixc(TX, B, 0.25f), 1, 5); return; }
  logo_d(x + w - pad - LOGO_W / 6, y + pad, 6, TX, 205);
  if (name && name[0]) text(F.small, name, x + pad, y + h - pad - 44, TX, 1, 3);
  if (since && since[0]) { char sv[32]; snprintf(sv, sizeof sv, "CREW SINCE %s", since); text(F.tiny, sv, x + pad, y + h - pad, mixc(TX, B, 0.3f), 1, 4); }
}
/* QR code (the tile's own encoder) and its room-colour corner brackets (snap = 0-1, they fly in from 40 px out) */
static void draw_qr(const char *t, int x, int y, int size) {
  static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
  if (!t[0] || !qrcodegen_encodeText(t, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return;
  int q = qrcodegen_getSize(qr), cell = size / (q + 6), off = (size - cell * q) / 2;
  rect(x, y, size, size, WHITE);
  for (int r = 0; r < q; r++) for (int c = 0; c < q; c++) if (qrcodegen_getModule(qr, c, r)) rect(x + off + c * cell, y + off + r * cell, cell, cell, BLACK);
}
static void brackets(int x, int y, int s, Color c, int a, float snap) {
  int o = 32 + (int)(40 * (1 - ease(snap))), L = 66, T = 5;
  int X0 = x - o, Y0 = y - o, X1 = x + s + o, Y1 = y + s + o;
  recta(X0, Y0, L, T, c, a); recta(X0, Y0, T, L, c, a); recta(X1 - L, Y0, L, T, c, a); recta(X1 - T, Y0, T, L, c, a);
  recta(X0, Y1 - T, L, T, c, a); recta(X0, Y1 - L, T, L, c, a); recta(X1 - L, Y1 - T, L, T, c, a); recta(X1 - T, Y1 - L, T, L, c, a);
}

/* ---- screen state (what the engine + the helper said) ---- */
struct TScr { char room[32], rname[32], url[200], crew[48], since[8], title[80], line[200], line2[200], err[96], st[24]; Color AC; int busy, done; };

/* positions (Karen's mockups, 1280 wide × 1.5) */
static const Color LAV = { 189, 181, 211 }, SUBC = { 217, 211, 234 };
static const int LX = 130, LW = 520, RX = 1790, QX = 1437, QY = 282, QS = 300, SCX = 717, SCY = 250, SCW = 498, SCH = 312, SDX = 31, SDY = 82;
static const int WCX = 768, WCY = 250, WCW = 575, WCH = 362;   /* the crew card on the signed-in screen */
static void room_name(const Fonts &F, const TScr &S) {
  if (S.rname[0] || S.room[0]) { char up[32]; snprintf(up, sizeof up, "%s", S.rname[0] ? S.rname : S.room); for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    int w = textw(F.pixs, up, 6); text(F.pixs, up, RX - w, 118, S.AC, 1, 6); }
}
static void top_bits(const Fonts &F, const TScr &S) { recta(0, 0, W, 8, S.AC, 255); logo_d(LX, 85, 2, WHITE, 255); room_name(F, S); }   /* top line, logo, room name top-right */
/* "Not saving today? Press [PS] to play." – a word in [ ] gets a small rounded box */
static void ps_line(const Fonts &F, const char *s, int x, int y, Color c) {
  char b[96]; snprintf(b, sizeof b, "%s", s); char *o = strchr(b, '['), *e = o ? strchr(o, ']') : NULL;
  if (!o || !e) { text(F.body, b, x, y, c); return; }
  *o = 0; *e = 0; x = text(F.body, b, x, y, c) + 6; int w = textw(F.body, o + 1); frame_box(x, y - 26, w + 16, 34, 2, c, 200); text(F.body, o + 1, x + 8, y, c); text(F.body, e + 1, x + w + 22, y, c);
}
/* "Members scan to log in" with its first word in the room colour */
static void two_tone(const Fonts &F, const char *s, int cxm, int y, Color ac) {
  const char *sp = strchr(s, ' '); int w = textw(F.body, s); if (!sp) { text(F.body, s, cxm - w / 2, y, INK); return; }
  char a[48]; snprintf(a, sizeof a, "%.*s", (int)(sp - s), s); if (!strcmp(a, "New")) { const char *sp2 = strchr(sp + 1, ' '); if (sp2) { snprintf(a, sizeof a, "%.*s", (int)(sp2 - s), s); sp = sp2; } }
  int x = text(F.body, a, cxm - w / 2, y, ac); text(F.body, sp, x, y, INK);
}

/* the scan screen; t = seconds since it opened (intro ~1.6 s: top line draws → words rise 0.08 s apart → cards deal in back to
   front 0.12 s apart → QR corners snap in → one sweep across the front card); t >= 3 = still picture */
static void draw_scan(const Fonts &F, const TCfg &C, const TScr &S, double t) {
  Color AC = S.AC; float k; int y;
  k = ease((float)(t / 0.45)); GA = 255; recta(0, 0, (int)(W * k), 8, AC, 255); logo_d(LX, 85, 2, WHITE, 255);
  k = ease((float)((t - 0.2) / 0.4)); GA = (int)(255 * k); room_name(F, S);
  const double w0 = 0.30;
  k = ease((float)((t - w0) / 0.4)); GA = (int)(255 * k); text(F.pixs, C.s_kick, LX, 262 + (int)(14 * (1 - k)), LAV, 1, 5);
  k = ease((float)((t - w0 - 0.08) / 0.4)); GA = (int)(255 * k); y = wrap(F.big, C.s_title, LX, 352 + (int)(14 * (1 - k)), LW, 76, INK);
  k = ease((float)((t - w0 - 0.16) / 0.4)); GA = (int)(255 * k); y = wrap(F.body, C.s_sub, LX, y + 14 + (int)(14 * (1 - k)), LW - 40, 40, SUBC);
  if (S.title[0] || (strcmp(S.st, "ready") && S.line[0])) { k = ease((float)((t - w0 - 0.24) / 0.4)); GA = (int)(255 * k);   /* reset / can't reach / signed in for this session */
    Color sc = !strcmp(S.st, "off") ? RED : !strcmp(S.st, "done") ? GREEN : AC; y += 24;
    if (S.title[0]) y = wrap(F.body, S.title, LX, y + 6, LW, 40, sc);
    y = wrap(F.tiny, S.line, LX, y + 6, LW, 32, S.title[0] ? INK : sc); if (S.line2[0]) wrap(F.tiny, S.line2, LX, y + 4, LW, 32, DIM); }
  if (C.s_foot[0]) { k = ease((float)((t - w0 - 0.32) / 0.4)); GA = (int)(255 * k); recta(LX, 696, LW - 40, 1, WHITE, 40); text(F.body, C.s_foot, LX, 742, LAV); }
  for (int i = 0; i < 3; i++) { double d0 = 0.5 + i * 0.12; k = ease((float)((t - d0) / 0.5)); if (k <= 0) continue; GA = (int)(255 * k);   /* cards deal in */
    int front = i == 2; double sw = t - 1.3; int sweep = front && sw > 0 && sw < 0.8 ? (int)(-80 + sw / 0.8 * (SCW + 160)) : -9999;
    card(F, SCX + i * SDX, SCY + i * SDY + (int)(60 * (1 - k)), SCW, SCH, i, front ? 1 : 0, C.c[i], C.cn[i], NULL, NULL, 1.f, sweep); }
  k = ease((float)((t - 0.9) / 0.3)); GA = (int)(255 * k);
  if (S.url[0]) draw_qr(S.url, QX, QY, QS); else { recta(QX, QY, QS, QS, CARD, 255); text(F.tiny, "QR code appears when", QX + 40, QY + 140, DIM); text(F.tiny, "Round2 answers", QX + 70, QY + 176, DIM); }
  GA = 255; if (t > 1.05 && t < 2.0) brackets(QX, QY, QS, AC, 255, (float)((t - 1.05) / 0.22));   /* after the intro the tile draws them each frame (they breathe) */
  k = ease((float)((t - 1.0) / 0.4)); GA = (int)(255 * k); two_tone(F, C.s_qr1, QX + QS / 2, 690, AC); two_tone(F, C.s_qr2, QX + QS / 2, 730, AC);
  k = ease((float)((t - 1.1) / 0.4)); GA = (int)(255 * k); recta(LX, 913, RX - LX, 1, WHITE, 40);
  ps_line(F, C.s_ps, LX, 968, LAV); { int w3 = textw(F.body, C.s_url); text(F.body, C.s_url, RX - w3, 968, LAV); }
  GA = 255; if (S.err[0]) text(F.tiny, S.err, LX, 1056, DIM);
}
/* the signed-in screen ("Welcome back, <crew>"); t = seconds since it opened (intro ~2 s: the crew card rises → its dot grid
   lights up left to right → one sweep → the words). The profiles list + bar are drawn each frame by the tile itself. */
static void draw_signed(const Fonts &F, const TCfg &C, const TScr &S, double t) {
  float k; char b[200]; GA = 255; top_bits(F, S);
  k = ease((float)(t / 0.7)); GA = (int)(255 * k);
  { double sw = t - 1.2; int sweep = sw > 0 && sw < 0.8 ? (int)(-80 + sw / 0.8 * (WCW + 160)) : -9999; float dots = t < 0.6 ? 0 : (float)((t - 0.6) / 0.6);
    card(F, WCX, WCY + (int)(40 * (1 - k)), WCW, WCH, 0, 2, "ROUND2 CREW", "", S.crew[0] ? S.crew : "YOUR CREW", S.since, dots > 1 ? 1 : dots, sweep); }
  k = ease((float)((t - 1.5) / 0.5)); GA = (int)(255 * k); int dy = (int)(14 * (1 - k)), y;
  text(F.tiny, C.w_kick, LX, 262 + dy, CARD_T[0], 1, 6);
  put_crew(b, sizeof b, S.done ? C.w_ready : C.w_title, S.crew); y = wrap(F.big, b, LX, 342 + dy, LW + 40, 76, S.done ? GREEN : INK);
  put_crew(b, sizeof b, S.done ? C.w_ready2 : C.w_sub, S.crew); y = wrap(F.body, b, LX, y + 14, LW + 40, 40, SUBC);
  if (S.line[0] && !S.done) wrap(F.tiny, S.line, LX, y + 18, LW + 40, 32, S.AC);
  GA = 255; if (S.err[0]) text(F.tiny, S.err, LX, 1056, DIM);
}

/* ---- the profiles list on the signed-in screen (mockup: name left, Ready / Loading / Next right, thin lines). Owner 21 Nov 11:54pm
   wanted it to feel alive, so: each row starts grey and lights up in order as it's ready (a short room-colour flash, a tick draws
   itself), a "Player 1" row turns into the member's name, and the row being loaded has a moving shine + "Loading..." dots ---- */
static const int PX0 = 1468, PY0 = 296, PW = RX - PX0, PH = 66;
struct TRows { int bn; char bst[5], bcur[5][32], btgt[5][32], bwho[5][32]; int lit[5]; double lit_t[5], last_lit; };
static void draw_rows(const Fonts &F, Color AC, TRows &B, double t) {
  GA = 255; text(F.pixs, "PROFILES", PX0, PY0 - 34, LAV, 1, 4);
  for (int i = 0; i < B.bn; i++) { if (B.lit[i]) continue; char c = B.bst[i]; if ((c == 'd' || c == 'f' || c == 'u') && t - B.last_lit >= 0.45) { B.lit[i] = 1; B.lit_t[i] = t; B.last_lit = t; } break; }
  recta(PX0, PY0, PW, 1, WHITE, 40);
  for (int i = 0; i < B.bn; i++) {
    int y = PY0 + i * PH, L = B.lit[i], active = !L && (B.bst[i] == 'c' || B.bst[i] == 'l'); double since = L ? t - B.lit_t[i] : 0; char c = B.bst[i];
    recta(PX0, y + PH, PW, 1, WHITE, 40);
    if (L && since < 0.6) recta(PX0, y + 1, PW, PH - 1, AC, (int)(70 * (1 - since / 0.6)));
    if (active) { double ph = fmod(t, 1.6) / 1.6; int sx = PX0 - 120 + (int)(ph * (PW + 240));
      for (int xx = sx - 70; xx < sx + 70; xx++) { if (xx < PX0 || xx >= PX0 + PW) continue; int a = 26 - abs(xx - sx) * 26 / 70; for (int yy = y + 1; yy < y + PH; yy++) blend(xx, yy, WHITE, a); } }
    int by = y + PH / 2 + 11; const char *nm = L ? B.btgt[i] : B.bcur[i];
    if (L && strcmp(B.bcur[i], B.btgt[i]) && since < 0.55) { float k = (float)(since / 0.55);   /* "Player 1" turns into the member */
      text(F.body, B.bcur[i], PX0, by - (int)(k * 22), mixc(DIM, BG, k)); text(F.body, B.btgt[i], PX0, by + 22 - (int)(k * 22), mixc(BG, INK, k)); }
    else text(F.body, nm, PX0, by, L ? INK : (active ? INK : DIM));
    char ln[48]; Color lc = DIM;
    if (L) { if (c == 'f') { snprintf(ln, sizeof ln, "Failed - ask staff"); lc = RED; } else { snprintf(ln, sizeof ln, "Ready"); lc = GREEN; } }
    else if (c == 'c') { snprintf(ln, sizeof ln, "Clearing"); lc = AC; }
    else if (c == 'l') { snprintf(ln, sizeof ln, "Loading"); lc = AC; }
    else if (c == 'w') snprintf(ln, sizeof ln, "Next");
    else snprintf(ln, sizeof ln, "Waiting");
    if (active) { int n = (int)(t * 2.5) % 4; size_t l = strlen(ln); for (int d = 0; d < n && l < sizeof ln - 1; d++) ln[l++] = '.'; ln[l] = 0; }
    int lw = textw(F.body, active ? (c == 'l' ? "Loading..." : "Clearing...") : ln); text(F.body, ln, RX - lw, by, lc);
    if (L && since < 0.4) { float g = (float)(since / 0.4); int kx = RX - lw - 34, ky = by - 14; for (int s2 = 0; s2 < (int)(8 * g); s2++) rect(kx + s2, ky + s2, 4, 4, lc); }
  }
}
#endif
