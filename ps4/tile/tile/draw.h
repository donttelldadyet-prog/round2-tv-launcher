/* Round2 tile 0.16 – the screens (27 Nov 2026, Karen's "PS4 save screen v2 – same look as the member page",
   round2sg-home design/tile/save-screen-v2.html). Everything that paints pixels lives here, so the desktop simulator
   (ps4/tile/sim) draws exactly what the PS4 draws. Words and timings come from the engine's /ps4/tilecfg (design/tile.json).
   Four screens, 1920x1080, positions measured from the mockup at full size:
     1 Scan     – "SAVE POINT / Pick up where you left off." left, a glass orb with "SAVE POINT" curved over it and the 7 room
                  marbles orbiting it in the middle, the QR with room-colour corners right, "Not saving today? Press (PS) to play."
     2 Loading  – "ROUND2 CREW / Hey, <crew>!" left, the crew card (CREW SINCE) in the middle, "Who's playing in <room>" rows with
                  Waiting / Loading / Ready badges right, the bar + "n of m profiles ready" under them
     3 Ready    – "SAVES LOADED / <crew> is all set." left, the orb full in the room colour with "GAME SAVED" over it (bounce,
                  sparkles, two rippling rings), the rows all Ready right
     4 Next     – "WHAT HAPPENS NEXT / Ready, player one." left, 4 steps with room-colour marble numbers sliding in, "Closing in N"
   The PS4 draws in software, so the design's blur is left out and each screen's still parts are drawn once (main.cpp keeps
   them in BASE); only the orb, marbles, rows, bar, sweep and countdown are drawn every frame. */
#ifndef R2_DRAW_H
#define R2_DRAW_H
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "logo.h"

#define W 1920
#define H 1080
static Color BG = { 8, 7, 12 }, CARD = { 20, 17, 31 }, INK = { 244, 241, 251 }, DIM = { 167, 159, 189 }, GOLD = { 245, 196, 0 },
             WHITE = { 255, 255, 255 }, BLACK = { 0, 0, 0 }, RED = { 255, 122, 122 }, GREEN = { 63, 224, 160 };
static const Color SUBC = { 207, 201, 222 }, LAV = { 167, 159, 189 }, URLC = { 111, 104, 132 }, TMC = { 143, 136, 163 }, YEL = { 255, 211, 110 },
                   DARK = { 11, 9, 20 };
/* the 7 rooms' colours (design/rooms.json) – the marbles orbiting the orb, the sparkles */
static const Color ROOMC[7] = { { 0xE8, 0x43, 0x6A }, { 0xE9, 0xE7, 0xEF }, { 0x24, 0xB4, 0x9A }, { 0x7A, 0x5B, 0xE0 }, { 0xF2, 0xC2, 0x1B }, { 0x4D, 0xB8, 0x48 }, { 0xE0, 0x40, 0x2F } };

/* ---- drawing straight into a frame buffer (alpha-blended); GA = global strength for fades (0-255) ---- */
static uint32_t *FB; static int GA = 255;
static int CX0 = 0, CY0 = 0, CX1 = W, CY1 = H;   /* clip box */
static inline void blend(int x, int y, Color c, int a) {
  if (x < CX0 || y < CY0 || x >= CX1 || y >= CY1) return; if (GA < 255) a = a * GA / 255; if (a <= 0) return;
  uint32_t *p = FB + y * W + x;
  if (a >= 255) { *p = 0xFF000000u | (c.r << 16) | (c.g << 8) | c.b; return; }
  uint32_t o = *p; int r = (o >> 16) & 255, g = (o >> 8) & 255, b = o & 255;
  r += (c.r - r) * a / 255; g += (c.g - g) * a / 255; b += (c.b - b) * a / 255;
  *p = 0xFF000000u | (r << 16) | (g << 8) | b;
}
static void clip(int x0, int y0, int x1, int y1) { CX0 = x0 < 0 ? 0 : x0; CY0 = y0 < 0 ? 0 : y0; CX1 = x1 > W ? W : x1; CY1 = y1 > H ? H : y1; }
static void noclip(void) { CX0 = 0; CY0 = 0; CX1 = W; CY1 = H; }
static void rect(int x, int y, int w, int h, Color c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) blend(i, j, c, 255); }
static void recta(int x, int y, int w, int h, Color c, int a) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) blend(i, j, c, a); }
static Color mixc(Color a, Color b, float k) { if (k < 0) k = 0; if (k > 1) k = 1; Color c = { (uint8_t)(a.r + (b.r - a.r) * k), (uint8_t)(a.g + (b.g - a.g) * k), (uint8_t)(a.b + (b.b - a.b) * k) }; return c; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ease(float t) { if (t <= 0) return 0; if (t >= 1) return 1; return 1 - (1 - t) * (1 - t) * (1 - t); }   /* calm ease-out, no bounce */

/* ---- text. y = baseline; sp = extra letter spacing; returns the x after it (draw = 0: just measure). c2 (optional) = a top→bottom
   gradient (the crew name on the card) ---- */
static int textg(FT_Face f, const char *s, int x, int y, Color c, Color c2, int grad, int draw, int sp) {
  if (!f) return x;
  int asc = f->size ? (int)(f->size->metrics.ascender >> 6) : 20;
  for (; *s; s++) {
    unsigned ch = (unsigned char)*s; if (ch >= 128) ch = '?';
    if (FT_Load_Glyph(f, FT_Get_Char_Index(f, ch), FT_LOAD_DEFAULT) || FT_Render_Glyph(f->glyph, ft_render_mode_normal)) continue;
    FT_GlyphSlot g = f->glyph; FT_Bitmap *b = &g->bitmap;
    if (draw) for (int r = 0; r < (int)b->rows; r++) { Color cc = c; if (grad) cc = mixc(c, c2, (float)(asc - g->bitmap_top + r) / (asc > 0 ? asc : 1));
      for (int k = 0; k < (int)b->width; k++) blend(x + g->bitmap_left + k, y - g->bitmap_top + r, cc, ((unsigned char *)b->buffer)[r * b->pitch + k]); }
    x += (g->advance.x >> 6) + sp;
  }
  return x;
}
static int text(FT_Face f, const char *s, int x, int y, Color c, int draw = 1, int sp = 0) { return textg(f, s, x, y, c, c, 0, draw, sp); }
static int textw(FT_Face f, const char *s, int sp = 0) { int w = text(f, s, 0, 0, WHITE, 0, sp); return *s && sp ? w - sp : w; }
/* the baseline of a line box (CSS-like: the box is lh tall, the letters sit in its middle) */
static int bl(FT_Face f, int top, int lh) { if (!f || !f->size) return top + lh * 3 / 4; int a = (int)(f->size->metrics.ascender >> 6), d = (int)(-(f->size->metrics.descender >> 6)); return top + (lh - (a + d)) / 2 + a; }
/* word wrap to a pixel width; top = the top of the first line box; returns the top under the last line */
static int wrap(FT_Face f, const char *s, int x, int top, int maxw, int lh, Color c, int sp = 0) {
  char line[256] = "", word[128]; const char *p = s;
  while (*p) {
    int n = 0; while (p[n] && p[n] != ' ' && n < 127) n++; snprintf(word, sizeof word, "%.*s", n, p); p += n; while (*p == ' ') p++;
    char test[256]; snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && textw(f, test, sp) > maxw) { text(f, line, x, bl(f, top, lh), c, 1, sp); top += lh; snprintf(line, sizeof line, "%s", word); }
    else snprintf(line, sizeof line, "%s", test);
  }
  if (line[0]) { text(f, line, x, bl(f, top, lh), c, 1, sp); top += lh; }
  return top;
}

/* ---- shapes with soft (anti-aliased) edges ---- */
static float rr_cov(float px, float py, float x, float y, float w, float h, float r) {   /* how much of pixel (px,py) is inside a rounded box */
  float cx = px + 0.5f, cy = py + 0.5f;
  if (cx < x - 0.5f || cy < y - 0.5f || cx > x + w + 0.5f || cy > y + h + 0.5f) return 0;
  float dx = cx < x + r ? x + r - cx : cx > x + w - r ? cx - (x + w - r) : 0, dy = cy < y + r ? y + r - cy : cy > y + h - r ? cy - (y + h - r) : 0;
  if (dx > 0 && dy > 0) return clampf(r - sqrtf(dx * dx + dy * dy) + 0.5f, 0, 1);
  float e = cx - x; if (cx > x + w - 0.5f) e = x + w - cx + 0.5f; else if (e < 0.5f) e += 0.5f; else e = 1;
  float f2 = cy - y; if (cy > y + h - 0.5f) f2 = y + h - cy + 0.5f; else if (f2 < 0.5f) f2 += 0.5f; else f2 = 1;
  return clampf(e < f2 ? e : f2, 0, 1);
}
static void rrect(int x, int y, int w, int h, int r, Color c, int a) {
  for (int j = y - 1; j <= y + h; j++) { int midr = j >= y + r && j < y + h - r && j >= y + 1 && j < y + h - 1;
    for (int i = x - 1; i <= x + w; i++) {
      if (i >= x + 1 && i < x + w - 1 && j >= y + 1 && j < y + h - 1 && (midr || (i >= x + r && i < x + w - r))) { blend(i, j, c, a); continue; }
      float k = rr_cov((float)i, (float)j, (float)x, (float)y, (float)w, (float)h, (float)r); if (k > 0) blend(i, j, c, (int)(a * k)); } } }
static void rrline(int x, int y, int w, int h, int r, int t, Color c, int a) {   /* an outline t px thick, inside the box */
  for (int j = y - 1; j <= y + h; j++) for (int i = x - 1; i <= x + w; i++) {
    if (i > x + r + t && i < x + w - r - t && j > y + t && j < y + h - t) continue;
    float k = rr_cov((float)i, (float)j, (float)x, (float)y, (float)w, (float)h, (float)r) - rr_cov((float)i, (float)j, (float)(x + t), (float)(y + t), (float)(w - 2 * t), (float)(h - 2 * t), (float)(r > t ? r - t : 0));
    if (k > 0) blend(i, j, c, (int)(a * k)); } }
static void circ(float cx, float cy, float r, Color c, int a) { rrect((int)(cx - r), (int)(cy - r), (int)(2 * r), (int)(2 * r), (int)r, c, a); }
/* a ring (annulus) from radius r to r+t, drawn row by row (cheap enough every frame) */
static void annulus(float cx, float cy, float r, float t, Color c, int a) {
  if (r <= 0 || a <= 0) return; float ro = r + t;
  for (int y = (int)(cy - ro) - 1; y <= (int)(cy + ro) + 1; y++) { float dy = y + 0.5f - cy; if (fabsf(dy) > ro + 1) continue;
    float xo = sqrtf(fmaxf(0, (ro + 1) * (ro + 1) - dy * dy)), xi = fabsf(dy) < r - 1 ? sqrtf((r - 1) * (r - 1) - dy * dy) : 0;
    for (int side = -1; side <= 1; side += 2) { int x0 = (int)(cx + side * xi), x1 = (int)(cx + side * xo); if (x0 > x1) { int q = x0; x0 = x1; x1 = q; }
      for (int x = x0 - 1; x <= x1 + 1; x++) { if (side > 0 && x < (int)cx) continue; if (side < 0 && x >= (int)cx) continue; float dx = x + 0.5f - cx, d = sqrtf(dx * dx + dy * dy);
        float k = clampf(fminf(d - r + 0.5f, ro - d + 0.5f), 0, 1); if (k > 0) blend(x, y, c, (int)(a * k)); } } }
}
/* a soft glow round a box (the CSS box-shadow, no real blur): strength fades over `soft` px outside (x,y,w,h) */
static void glow_box(int x, int y, int w, int h, int r, int soft, Color c, int a) {
  for (int j = y - soft; j < y + h + soft; j++) for (int i = x - soft; i < x + w + soft; i++) {
    float dx = i < x + r ? x + r - i : i > x + w - r ? i - (x + w - r) : 0, dy = j < y + r ? y + r - j : j > y + h - r ? j - (y + h - r) : 0, d = sqrtf(dx * dx + dy * dy) - r;
    if (d < 0) d = 0; if (d >= soft) continue; float k = 1 - d / soft; blend(i, j, c, (int)(a * k * k)); } }
static void glow_disc(float cx, float cy, float r, float soft, Color c, int a) {
  for (int j = (int)(cy - r - soft); j <= (int)(cy + r + soft); j++) for (int i = (int)(cx - r - soft); i <= (int)(cx + r + soft); i++) {
    float dx = i - cx, dy = j - cy, d = sqrtf(dx * dx + dy * dy) - r; if (d < 0) d = 0; if (d >= soft) continue; float k = 1 - d / soft; blend(i, j, c, (int)(a * k * k)); } }

/* the Round2 logo (640x163 alpha mask) at any height, colour c at strength a */
static void logo_h(int x, int y, int h, Color c, int a) {
  float s = (float)LOGO_H / h; int w = (int)(LOGO_W / s + 0.5f);
  for (int j = 0; j < h; j++) { int y0 = (int)(j * s), y1 = (int)((j + 1) * s); if (y1 <= y0) y1 = y0 + 1; if (y1 > LOGO_H) y1 = LOGO_H;
    for (int i = 0; i < w; i++) { int x0 = (int)(i * s), x1 = (int)((i + 1) * s); if (x1 <= x0) x1 = x0 + 1; if (x1 > LOGO_W) x1 = LOGO_W;
      int sum = 0, n = 0; for (int v = y0; v < y1; v++) for (int u = x0; u < x1; u++) { sum += LOGO_A[v * LOGO_W + u]; n++; }
      int la = n ? sum / n : 0; if (la) blend(x + i, y + j, c, la * a / 255); } }
}
static int logo_wh(int h) { return (int)(LOGO_W * h / (float)LOGO_H + 0.5f); }

/* ---- words + timings (engine /ps4/tilecfg = design/tile.json; defaults = Karen's v2 words). 0.16 uses the existing keys (no engine
   change needed): c1 = the word over the orb on the scan screen, c2 = the word over the full orb, c3 = the crew card's label,
   c1n = the "all set" screen's small heading, c2n = the "what happens next" big line, c3n = the rows' heading ({room} = the room).
   n_title = the "what happens next" small heading, n1-n4 = the steps ("main line|small line"; a leading "1." is dropped), n5 unused.
   [PS] in a text = the round PS button. ---- */
struct TCfg { char s_kick[32], s_title[64], s_sub[120], s_foot[88], s_qr1[40], s_qr2[40], s_ps[56], s_url[36], c[3][16], cn[3][24],
  w_kick[32], w_title[64], w_sub[120], w_ready[64], w_ready2[96], n_title[48], n[5][104]; float t_burst, t_next;
  char v_lab[20], v_wait[40], v_conn[40], v_load[40], v_insert[20], v_hi[28], v_read[16], v_found[24], v_all[20], v_saved[20], v_press[14]; };   /* 0.20: v4 words */
static void cfg_default(TCfg *c) {
  memset(c, 0, sizeof *c);
  strcpy(c->s_kick, "SAVE POINT"); strcpy(c->s_title, "Pick up where you left off."); strcpy(c->s_sub, "7 rooms. One save. Carry on in any of them.");
  strcpy(c->s_qr1, "Scan to load your save"); strcpy(c->s_qr2, "New here? Scan to create one"); strcpy(c->s_ps, "Not saving today? Press [PS] to play."); strcpy(c->s_url, "book.round2.sg/m");
  strcpy(c->c[0], "SAVE POINT"); strcpy(c->c[1], "GAME SAVED"); strcpy(c->c[2], "ROUND2 CREW");
  strcpy(c->cn[0], "SAVES LOADED"); strcpy(c->cn[1], "Ready, player one."); c->cn[2][0] = 0;
  strcpy(c->w_kick, "ROUND2 CREW"); strcpy(c->w_title, "Hey, {crew}!"); strcpy(c->w_sub, "Loading your saves onto this PS4. Please don't start a game yet.");
  strcpy(c->w_ready, "{crew} is all set."); strcpy(c->w_ready2, "Press [PS] on your controller and pick your profile.");
  strcpy(c->n_title, "WHAT HAPPENS NEXT"); strcpy(c->n[0], "This screen closes by itself."); strcpy(c->n[1], "Your TV shows \"Press PS button\".");
  strcpy(c->n[2], "Press [PS] on your controller.|Pick your profile - it has your name and picture."); strcpy(c->n[3], "More players?|Each one presses PS on their own controller and picks theirs.");
  c->t_burst = 4.f; c->t_next = 6.f;
  strcpy(c->v_lab, "MEMBER SAVE"); strcpy(c->v_wait, "Waiting for your phone"); strcpy(c->v_conn, "Phone connected"); strcpy(c->v_load, "Loading your save");
  strcpy(c->v_insert, "INSERT SAVE"); strcpy(c->v_hi, "HI, {crew}!"); strcpy(c->v_read, "READING"); strcpy(c->v_found, "SAVE FOUND {n}/7"); strcpy(c->v_all, "ALL 7 ROOMS!");
  strcpy(c->v_saved, "GAME SAVED"); strcpy(c->v_press, "PRESS");
}
static void put_crew(char *out, int n, const char *tpl, const char *crew) {   /* "{crew}" → the crew's name */
  const char *p = strstr(tpl, "{crew}"); if (!p) { snprintf(out, n, "%s", tpl); return; }
  snprintf(out, n, "%.*s%s%s", (int)(p - tpl), tpl, crew[0] ? crew : "your crew", p + 6);
}

/* ---- the background: the room colour glowing from just above the middle, fading to near-black (the mockup's radial gradient) ---- */
static void bg_v2(uint32_t *dst, Color AC) {
  Color s0 = mixc({ 8, 7, 12 }, AC, 0.30f), s1 = mixc({ 8, 7, 12 }, AC, 0.10f), s2 = { 7, 6, 10 }, s3 = { 4, 3, 6 };
  const float cx = W * 0.5f, cy = H * 0.42f, rx = W * 0.6f, ry = H * 0.7f;
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    float dx = (x - cx) / rx, dy = (y - cy) / ry, d = sqrtf(dx * dx + dy * dy); Color c;
    if (d < 0.45f) c = mixc(s0, s1, d / 0.45f); else if (d < 0.75f) c = mixc(s1, s2, (d - 0.45f) / 0.3f); else if (d < 1) c = mixc(s2, s3, (d - 0.75f) / 0.25f); else c = s3;
    dst[y * W + x] = 0xFF000000u | (c.r << 16) | (c.g << 8) | c.b; }
}

/* ---- fonts (px sizes from the mockup) ---- */
struct Fonts { FT_Face big, mid, small, tiny, pix, pixs, body,   /* 0.15's set (small = SemiBold 34, tiny = 24, body = 30) */
  h1, h1r, sb38, sb30, sb26, sb24, sb20, sb16, sb15, r32, r26, r22, r20, r17, r12, pix24, pix22, pix18, pix12; };
typedef FT_Face (*FontLoad)(const char *file, int px, void *ctx);
static void fonts_load(Fonts &F, FontLoad L, void *x) {
  const char *SB = "ChakraPetch-SemiBold.ttf", *RG = "ChakraPetch-Regular.ttf", *PX = "PressStart2P-Regular.ttf";
  F.big = L(SB, 72, x); F.mid = L(RG, 44, x); F.small = L(SB, 34, x); F.tiny = L(RG, 24, x); F.pix = L(PX, 64, x); F.pixs = L(PX, 20, x); F.body = L(RG, 30, x);
  F.h1 = L(SB, 76, x); F.h1r = L(RG, 76, x); F.sb38 = L(SB, 38, x); F.sb30 = L(SB, 30, x); F.sb26 = L(SB, 26, x); F.sb24 = L(SB, 24, x); F.sb20 = L(SB, 20, x); F.sb16 = L(SB, 16, x); F.sb15 = L(SB, 15, x);
  F.r32 = L(RG, 32, x); F.r26 = L(RG, 26, x); F.r22 = L(RG, 22, x); F.r20 = L(RG, 20, x); F.r17 = L(RG, 17, x); F.r12 = L(RG, 12, x);
  F.pix24 = L(PX, 24, x); F.pix22 = L(PX, 22, x); F.pix18 = L(PX, 18, x); F.pix12 = L(PX, 13, x);
}

/* ---- screen state (what the engine + the helper said) ---- */
struct TScr { char room[32], rname[32], url[200], crew[48], since[8], valid[8], title[80], line[200], line2[200], err[96], st[24], plab[96]; Color AC; int busy, done; float prog; int pmode; };

/* positions (mockup, 1920x1080) */
static const int LX = 130, RX = 1790, OCX = 960, OCY = 570, ORB_R = 165, ARC_R = 260, QX = 1440, QY = 330, QS = 300;
static const int KX = 748, KY = 389, KW = 560, KH = 353;                 /* the crew card (0.18: v3 – between the words and the list, top = first row) */
static const int PX0 = 1370, PW = 420, PRH = 86, PRS = 98;               /* the rows */
static const int SX0 = 820, SY0 = 318, SW = 980;   /* 0.21: steps start level with the heading */                         /* the steps */

/* chrome: room-colour line on top, the logo (46 px high), the room's name in a pill top-right */
static void chrome(const Fonts &F, const TScr &S, float k_line = 1) {
  recta(0, 0, (int)(W * k_line), 8, S.AC, 255); logo_h(LX, 78, 46, WHITE, 242);
  if (S.rname[0] || S.room[0]) { char up[32]; snprintf(up, sizeof up, "%s", S.rname[0] ? S.rname : S.room); for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    int tw = textw(F.pix22, up, 3), pw = tw + 48, px = RX - pw; rrline(px, 80, pw, 54, 27, 2, S.AC, 140); text(F.pix22, up, px + 24, 107 + 11, S.AC, 1, 3); }
}
/* a small heading in the pixel font + a big title. Titles with {crew}: the crew's name is bright, the words around it dim on their
   own line ("Hey," / "Night Owls!", "Night Owls" / "is all set."); titles with a comma break after it ("Ready," / "player one.") */
static int heading(const Fonts &F, const char *kick, Color kc, const char *title, const char *crew, int top, int maxw, int dy = 0, int ga = 255) {
  GA = ga; text(F.pix24, kick, LX, top + 24 + dy, kc, 1, 3);
  int y = top + 46 + dy, lh = 80; Color dimc = INK;
  const char *p = strstr(title, "{crew}");
  if (p) { char pre[64], post[64], cr[64]; snprintf(pre, sizeof pre, "%.*s", (int)(p - title), title); snprintf(post, sizeof post, "%s", p + 6);
    for (int i = (int)strlen(pre) - 1; i >= 0 && pre[i] == ' '; i--) pre[i] = 0;
    snprintf(cr, sizeof cr, "%s", crew[0] ? crew : "your crew");
    if (post[0] && strchr("!.,?", post[0])) { char t[64]; snprintf(t, sizeof t, "%s%c", cr, post[0]); snprintf(cr, sizeof cr, "%s", t); memmove(post, post + 1, strlen(post)); }
    char *q = post; while (*q == ' ') q++;
    if (pre[0]) { GA = ga * 7 / 10; y = wrap(F.h1r, pre, LX, y, maxw, lh, dimc); }
    GA = ga; y = wrap(F.h1, cr, LX, y, maxw, lh, INK);
    if (*q) { GA = ga * 7 / 10; y = wrap(F.h1r, q, LX, y, maxw, lh, dimc); }
  } else { const char *cm = strstr(title, ", ");
    if (cm) { char a[64]; snprintf(a, sizeof a, "%.*s", (int)(cm - title) + 1, title); y = wrap(F.h1, a, LX, y, maxw, lh, INK); y = wrap(F.h1, cm + 2, LX, y, maxw, lh, INK); }
    else y = wrap(F.h1, title, LX, y, maxw, lh, INK); }
  GA = 255; return y;
}
/* the round PS button inside a line of text: "[PS]" in s. Returns the top under the text. d = button size, t = its line */
static int ps_text(const Fonts &F, FT_Face f, const char *s, int x, int top, int maxw, int lh, Color c, int d, int t, FT_Face bf) {
  char word[64]; const char *p = s; int cx = x, y = top, space = textw(f, " ");
  while (*p) {
    int n = 0; while (p[n] && p[n] != ' ' && n < 63) n++; snprintf(word, sizeof word, "%.*s", n, p); p += n; while (*p == ' ') p++;
    int isb = !strncmp(word, "[PS]", 4), ww = isb ? d + 12 : textw(f, word);
    if (cx > x && cx + ww > x + maxw) { cx = x; y += lh; }
    int mid = y + lh / 2;
    if (isb) { rrline(cx + 6, mid - d / 2, d, d, d / 2, t, INK, 255); int bw = textw(bf, "PS"); text(bf, "PS", cx + 6 + (d - bw) / 2, mid + 6, INK);
      if (word[4]) text(f, word + 4, cx + ww, bl(f, y, lh), c), ww += textw(f, word + 4); }
    else text(f, word, cx, bl(f, y, lh), c);
    cx += ww + space;
  }
  return y + lh;
}
/* curved words over the orb (pixel font, wide spacing), centred on the top of a circle of radius R round (cx,cy) */
static void arc_text(FT_Face f, const char *s, float cx, float cy, float R, Color c, int a) {
  if (!f) return; int sp = 7, L = textw(f, s, sp); float pos = 0;
  for (const char *q = s; *q; q++) {
    unsigned ch = (unsigned char)*q; if (FT_Load_Glyph(f, FT_Get_Char_Index(f, ch), FT_LOAD_DEFAULT) || FT_Render_Glyph(f->glyph, ft_render_mode_normal)) continue;
    FT_GlyphSlot g = f->glyph; FT_Bitmap *b = &g->bitmap; int adv = (g->advance.x >> 6);
    float mid = pos + adv / 2.f - L / 2.f, th = -1.5707963f + mid / R, ph = th + 1.5707963f;   /* angle on the circle, glyph turned to face out */
    float ax = cx + R * cosf(th), ay = cy + R * sinf(th), cs = cosf(ph), sn = sinf(ph);
    /* glyph pixel (u,v) relative to its baseline centre → rotate by ph → screen; draw by reverse-sampling a box round it */
    float gx0 = g->bitmap_left - adv / 2.f, gy0 = -g->bitmap_top; int bw = b->width, bh = b->rows, ext = (int)(sqrtf((float)(bw * bw + bh * bh)) + adv) + 2;
    for (int y = (int)ay - ext; y <= (int)ay + ext; y++) for (int x = (int)ax - ext; x <= (int)ax + ext; x++) {
      float dx = x + 0.5f - ax, dy = y + 0.5f - ay, u = dx * cs + dy * sn - gx0, v = -dx * sn + dy * cs - gy0;   /* inverse rotation */
      int iu = (int)floorf(u - 0.5f), iv = (int)floorf(v - 0.5f); float fu = u - 0.5f - iu, fv = v - 0.5f - iv; float acc = 0;
      for (int k = 0; k < 4; k++) { int uu = iu + (k & 1), vv = iv + (k >> 1); if (uu < 0 || vv < 0 || uu >= bw || vv >= bh) continue;
        float wgt = ((k & 1) ? fu : 1 - fu) * ((k >> 1) ? fv : 1 - fv); acc += wgt * ((unsigned char *)b->buffer)[vv * b->pitch + uu]; }
      if (acc > 1) blend(x, y, c, (int)(acc * a / 255)); }
    pos += adv + sp;
  }
}

/* ---- the orb. glass (scan): clear with a bright rim; full (ready): filled in the room colour. sx/sy = squash for the bounce ---- */
typedef void (*PixPut)(int x, int y, Color c, int a, void *ctx);
static void put_fb(int x, int y, Color c, int a, void *) { blend(x, y, c, a); }
static void orb_core(float cx, float cy, float R, float sx, float sy, int full, Color AC, PixPut put, void *cx_) {
  float rx = R * sx, ry = R * sy; Color top = mixc(AC, WHITE, 0.18f), bot = mixc(AC, BLACK, 0.40f);
  const float cr = 0.8829f, sr = 0.4695f;   /* cos/sin 28deg: the shine is turned -28deg */
  for (int y = (int)(cy - ry) - 1; y <= (int)(cy + ry) + 1; y++) for (int x = (int)(cx - rx) - 1; x <= (int)(cx + rx) + 1; x++) {
    float u = (x + 0.5f - cx) / rx, v = (y + 0.5f - cy) / ry, d2 = u * u + v * v; if (d2 > 1.03f) continue;
    float d = sqrtf(d2), edge = (1 - d) * R, cov = clampf(edge + 0.5f, 0, 1); if (cov <= 0) continue;
    float yy = (v + 1) / 2;   /* 0 top .. 1 bottom */
    if (full) { Color c = yy < 0.45f ? mixc(top, AC, yy / 0.45f) : mixc(AC, bot, (yy - 0.45f) / 0.55f); put(x, y, c, (int)(255 * cov), cx_); }
    else { float t = clampf(((u * 0.34f) + (v * 0.94f) + 1) / 2, 0, 1), al = 0.15f + (0.03f - 0.15f) * clampf(t / 0.6f, 0, 1); put(x, y, WHITE, (int)(255 * al * cov), cx_);
      if (v > 0) { float sh = clampf(1 - edge / 60, 0, 1) * clampf(v * 1.4f, 0, 1); put(x, y, BLACK, (int)(90 * sh * cov), cx_); } }
    if (edge < 2.5f) put(x, y, WHITE, (int)((full ? 64 : 46) * cov), cx_);                                   /* thin inner rim */
    { float vt = (y + 0.5f - (cy + 3)) / ry, dt = u * u + vt * vt; if (v < 0 && dt > 1 && d2 <= 1) put(x, y, WHITE, (int)((full ? 190 : 166) * cov * clampf(-v * 1.3f, 0, 1)), cx_); }   /* top lip */
    { float ex = u * R + 57, ey = v * R + 108, qx = (ex * cr - ey * sr) / 56, qy = (ex * sr + ey * cr) / 23, q = qx * qx + qy * qy;   /* the shine */
      if (q < 1.6f) put(x, y, WHITE, (int)(102 * clampf((1.6f - q) / 1.0f, 0, 1) * cov), cx_); }
  }
}
/* the orb as a cached picture (colour + strength per pixel), worked out once per room colour by drawing it on black and on white */
struct OrbSpr { int ok, full; Color ac; float *k; Color *c; };
static OrbSpr OSP[2];
struct OrbBuild { float *b, *w; int ox, oy, sz; };
static void put_two(int x, int y, Color c, int a, void *p) { OrbBuild *o = (OrbBuild *)p; int i = x - o->ox, j = y - o->oy; if (i < 0 || j < 0 || i >= o->sz || j >= o->sz) return; float k = a / 255.f;
  float *B = o->b + 3 * (j * o->sz + i), *Wt = o->w + 3 * (j * o->sz + i); B[0] += (c.r - B[0]) * k; B[1] += (c.g - B[1]) * k; B[2] += (c.b - B[2]) * k; Wt[0] += (c.r - Wt[0]) * k; Wt[1] += (c.g - Wt[1]) * k; Wt[2] += (c.b - Wt[2]) * k; }
static void orb(float cx, float cy, float R, float sx, float sy, int full, Color AC) {
  if (sx != 1 || sy != 1 || (int)R != ORB_R) { orb_core(cx, cy, R, sx, sy, full, AC, put_fb, NULL); return; }
  const int sz = 2 * ORB_R + 8; OrbSpr &S = OSP[full ? 1 : 0];
  if (!S.ok || memcmp(&S.ac, &AC, sizeof AC)) {
    if (!S.k) { S.k = (float *)malloc(sizeof(float) * sz * sz); S.c = (Color *)malloc(sizeof(Color) * sz * sz); }
    float *b = (float *)calloc(3 * sz * sz, sizeof(float)), *w = (float *)malloc(sizeof(float) * 3 * sz * sz);
    if (!S.k || !S.c || !b || !w) { free(b); free(w); orb_core(cx, cy, R, 1, 1, full, AC, put_fb, NULL); return; }
    for (int i = 0; i < 3 * sz * sz; i++) w[i] = 255;
    OrbBuild ob = { b, w, 0, 0, sz }; orb_core(sz / 2.f, sz / 2.f, R, 1, 1, full, AC, put_two, &ob);
    for (int i = 0; i < sz * sz; i++) { float k = 1 - (w[3 * i] - b[3 * i]) / 255.f; if (k < 0.004f) { S.k[i] = 0; continue; } S.k[i] = k;
      Color c = { (uint8_t)clampf(b[3 * i] / k, 0, 255), (uint8_t)clampf(b[3 * i + 1] / k, 0, 255), (uint8_t)clampf(b[3 * i + 2] / k, 0, 255) }; S.c[i] = c; }
    free(b); free(w); S.ok = 1; S.ac = AC; S.full = full; }
  int x0 = (int)(cx - sz / 2.f), y0 = (int)(cy - sz / 2.f);
  for (int j = 0; j < sz; j++) for (int i = 0; i < sz; i++) { float k = S.k[j * sz + i]; if (k > 0) blend(x0 + i, y0 + j, S.c[j * sz + i], (int)(k * 255 + 0.5f)); }
}
/* a marble: white spot top-left, the colour, darker at the bottom edge, a soft glow of its colour */
static void marble(float cx, float cy, float r, Color c, int a, int glow = 1) {
  if (glow) glow_disc(cx, cy, r * 0.8f, r * 0.9f, c, a * 70 / 255);
  Color lt = mixc(c, WHITE, 0.45f), dk = mixc(c, BLACK, 0.40f);
  for (int y = (int)(cy - r) - 1; y <= (int)(cy + r) + 1; y++) for (int x = (int)(cx - r) - 1; x <= (int)(cx + r) + 1; x++) {
    float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy); float cov = clampf(r - d + 0.5f, 0, 1); if (cov <= 0) continue;
    float hx = x + 0.5f - (cx - r * 0.32f), hy = y + 0.5f - (cy - r * 0.40f), hd = sqrtf(hx * hx + hy * hy) / (2 * r);   /* distance from the light spot, 0..~1 */
    Color col = hd < 0.09f ? WHITE : hd < 0.22f ? mixc(WHITE, lt, (hd - 0.09f) / 0.13f) : hd < 0.55f ? mixc(lt, c, (hd - 0.22f) / 0.33f) : mixc(c, dk, (hd - 0.55f) / 0.45f);
    blend(x, y, col, (int)(a * cov)); }
}

/* ---- QR code (the tile's own encoder) on a white rounded square, room-colour corners top-left + bottom-right ---- */
static void draw_qr(const char *t, int x, int y, int size) {
  static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
  rrect(x, y, size, size, 18, WHITE, 255);
  if (!t[0] || !qrcodegen_encodeText(t, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return;
  int q = qrcodegen_getSize(qr), cell = (size - 36) / q, off = (size - cell * q) / 2;
  for (int r = 0; r < q; r++) for (int c = 0; c < q; c++) if (qrcodegen_getModule(qr, c, r)) rect(x + off + c * cell, y + off + r * cell, cell, cell, DARK);
}
static void corners(int x, int y, int s, Color c, int a, float snap) {
  int o = 20 + (int)(40 * (1 - ease(snap)));
  clip(x - o, y - o, x - o + 60, y - o + 60); rrline(x - o, y - o, 200, 200, 18, 6, c, a);
  clip(x + s + o - 60, y + s + o - 60, x + s + o, y + s + o); rrline(x + s + o - 200, y + s + o - 200, 200, 200, 18, 6, c, a); noclip();
}

/* ---------------- 1 · scan ---------------- */
/* still parts; t = seconds since it opened (intro ~1.6 s: top line draws, words rise 0.08 s apart, QR + corners snap in) */
static int g_wtx = 0;   /* 0.18: where the waiting dots go (set by scan_still) */
static void mc_deck(const TScr &S); static void mc_build(const TScr &S);   /* 0.20: the v4 memory card + save deck (below) */
struct Spr; static void spr_build_S(const TScr &S); static void spr_build_L(const TScr &S); static void deck_L(const TScr &S);   /* 0.21 */
static void scan_still(const Fonts &F, const TCfg &C, const TScr &S, double t) {
  Color AC = S.AC; float k; GA = 255; chrome(F, S, ease((float)(t / 0.45)));
  const double w0 = 0.3; int dy, y;
  k = ease((float)((t - w0) / 0.5)); dy = (int)(16 * (1 - k)); y = heading(F, C.s_kick, AC, C.s_title, "", 370, 600, dy, (int)(255 * k));
  k = ease((float)((t - w0 - 0.16) / 0.5)); GA = (int)(255 * k);
  { /* 0.18 (v3): "7 rooms. One save." / "Carry on in any of them." – a '|' in s_sub breaks the line; without one, a sub-line of two or
       more sentences breaks after the sentence that ends nearest the middle */
    char sb[128]; snprintf(sb, sizeof sb, "%s", C.s_sub); char *br = strchr(sb, '|');
    if (!br) { int L = (int)strlen(sb), best = -1; for (int i = 1; i < L - 1; i++) if ((sb[i] == '.' || sb[i] == '!' || sb[i] == '?') && sb[i + 1] == ' ' && (best < 0 || abs(i - L / 2) < abs(best - L / 2))) best = i;
      if (best > 0) { sb[best + 1] = '|'; br = sb + best + 1; } }
    int yy = y + 24 + (int)(16 * (1 - k));
    if (br) { *br = 0; yy = wrap(F.body, sb, LX, yy, 560, 42, SUBC); const char *r2 = br + 1; while (*r2 == ' ') r2++; y = wrap(F.body, r2, LX, yy, 560, 42, SUBC); }
    else y = wrap(F.body, sb, LX, yy, 560, 42, SUBC); }
  if (S.title[0] || (strcmp(S.st, "ready") && S.line[0])) { k = ease((float)((t - w0 - 0.24) / 0.5)); GA = (int)(255 * k);   /* reset / can't reach / signed in for this session */
    Color sc = !strcmp(S.st, "off") ? RED : !strcmp(S.st, "done") ? GREEN : AC; y += 28;
    if (S.title[0]) y = wrap(F.body, S.title, LX, y, 600, 40, sc);
    y = wrap(F.tiny, S.line, LX, y + 4, 600, 32, S.title[0] ? INK : sc); if (S.line2[0]) wrap(F.tiny, S.line2, LX, y + 4, 600, 32, DIM); }
  if (C.s_foot[0]) { GA = (int)(255 * k); text(F.tiny, C.s_foot, LX, 880, LAV); }
  GA = 255; mc_deck(S);   /* 0.20: replaces the orb + its arc word; 0.21: no card on Scan (it comes from the phone) */
  k = ease((float)((t - 0.8) / 0.35)); GA = (int)(255 * k);
  if (S.url[0]) draw_qr(S.url, QX, QY, QS); else { rrect(QX, QY, QS, QS, 18, CARD, 255); text(F.tiny, "QR code appears when", QX + 40, QY + 140, DIM); text(F.tiny, "Round2 answers", QX + 70, QY + 176, DIM); }
  GA = 255; if (t > 0.95) corners(QX, QY, QS, AC, 255, (float)((t - 0.95) / 0.25));
  k = ease((float)((t - 1.0) / 0.4)); GA = (int)(255 * k);
  { int w1 = textw(F.sb30, C.s_qr1); text(F.sb30, C.s_qr1, QX + QS / 2 - w1 / 2, bl(F.sb30, 670, 39), INK);
    const char *to = NULL, *q = C.s_qr2; while ((q = strstr(q, " to ")) != NULL) { to = q; q += 4; }   /* "New here? Scan to " + "create one" in the room colour */
    int w2 = textw(F.tiny, C.s_qr2), x2 = QX + QS / 2 - w2 / 2, b2 = bl(F.tiny, 719, 31);
    if (to) { char a[48]; snprintf(a, sizeof a, "%.*s", (int)(to - C.s_qr2) + 4, C.s_qr2); int xe = text(F.tiny, a, x2, b2, LAV); text(F.tiny, to + 4, xe, b2, AC); }
    else text(F.tiny, C.s_qr2, x2, b2, LAV);
    }   /* 0.20: "Waiting for your phone" / "Phone connected" / "Loading your save" + its dots are drawn live (scan_v4, scan_live) */
  k = ease((float)((t - 1.1) / 0.4)); GA = (int)(255 * k);
  ps_text(F, F.r26, C.s_ps, LX, 962, 1000, 46, LAV, 46, 2, F.sb15);
  { int w3 = textw(F.tiny, C.s_url, 1); text(F.tiny, C.s_url, RX - w3, bl(F.tiny, 969, 31), URLC, 1, 1); }
  GA = 255; if (S.err[0]) text(F.tiny, S.err, LX, 1056, DIM);
}
/* moving parts: the glass orb with the 7 room marbles orbiting it (those behind it go first, dimmer and smaller) */
static void scan_live(const TScr &S, double t, float fade = 1) {   /* 0.20: only the three dots before the words under the QR (the orb is gone) */
  int wtx = g_wtx; if (S.url[0] && wtx > 0) for (int d = 0; d < 3; d++) {
    float ph = (float)fmod(t - d * 0.2, 1.2) / 1.2f; if (ph < 0) ph += 1; float k = 0.5f - 0.5f * cosf(ph * 6.2831853f);
    GA = (int)(255 * fade); circ((float)(wtx + 5 + d * 20), 783.f, 5 * (0.7f + 0.3f * k), S.AC, (int)(255 * (0.25f + 0.75f * k))); GA = 255; }
}

/* ---------------- 2 · loading ---------------- */
/* the crew card – 0.18 (Karen's save screen v3, 27 Nov 2:52am "the one on the tv looks plain"): the phone's card. Brushed dark metal,
   room-colour glow top-right + bottom-left, gold chip with contact lines, label + logo, the crew's name (38 px), CREW SINCE / PLAYERS,
   a room-colour light line along the bottom edge and a soft coloured shadow under it. It stays still. The faces + PLAYERS count are
   drawn live (card_live) on top. */
static void crew_card(const Fonts &F, const TCfg &C, const TScr &S, int x, int y) {
  const int w = KW, h = KH, r = 30; Color c0 = { 48, 47, 55 }, c1 = { 23, 22, 27 }, c2 = { 12, 11, 15 };
  glow_box(x + 30, y + 70, w - 60, h - 60, 30, 70, S.AC, 80);   /* box-shadow 0 40px 90px -30px room 50% */
  for (int j = -6; j < 46; j++) for (int i = (int)(w * 0.08f) - 20; i < (int)(w * 0.92f) + 20; i++) {   /* the shadow pool under the card */
    float ex = (i - w / 2.f) / (w * 0.42f), ey = (j - 20) / 26.f, d = sqrtf(ex * ex + ey * ey); if (d >= 1) continue; blend(x + i, y + h + j, S.AC, (int)(95 * (1 - d) * (1 - d))); }
  const float bgx = w * 0.2f, bgy = h * 1.1f, bgr = 267;   /* bottom-left glow (circle at 20% 110%) */
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    float cov = rr_cov((float)(x + i), (float)(y + j), (float)x, (float)y, (float)w, (float)h, (float)r); if (cov <= 0) continue;
    float t = ((i - w / 2.f) * 0.342f + (j - h / 2.f) * 0.94f) / 523 + 0.5f; Color c = t < 0.55f ? mixc(c0, c1, t / 0.55f) : mixc(c1, c2, (t - 0.55f) / 0.45f);
    if (i % 5 < 2) c = mixc(c, WHITE, 0.02f);   /* brushed metal: fine vertical lines */
    float t2 = (i + j) * 0.7071f / 645.5f; if (t2 < 0.35f) c = mixc(c, WHITE, 0.12f * (1 - t2 / 0.35f));
    float ex = (i - w) / (1.2f * w), ey = j / (0.9f * h), d = sqrtf(ex * ex + ey * ey); if (d < 0.55f) c = mixc(c, S.AC, 0.22f * (1 - d / 0.55f));
    float gx = i - bgx, gy = j - bgy, gd = sqrtf(gx * gx + gy * gy); if (gd < bgr) c = mixc(c, S.AC, 0.21f * (1 - gd / bgr));
    blend(x + i, y + j, c, (int)(255 * cov)); }
  rrline(x, y, w, h, r, 2, WHITE, 31);
  { int ex0 = x + w / 10, ex1 = x + w - w / 10, ey = y + h - 3;   /* the light line along the bottom edge */
    for (int i = ex0; i < ex1; i++) { float q = (float)(i - ex0) / (ex1 - ex0), k = 1 - fabsf(q * 2 - 1); k = k * k * (3 - 2 * k);
      for (int g = -14; g < 0; g++) blend(i, ey + g, S.AC, (int)(60 * k * (1 + g / 14.f) * (1 + g / 14.f)));
      for (int j = 0; j < 3; j++) blend(i, ey + j, S.AC, (int)(255 * k)); } }
  text(F.r17, C.c[2], x + 34, bl(F.r17, y + 32, 22), { 189, 184, 201 }, 1, 6);
  logo_h(x + w - 34 - logo_wh(22), y + 32, 22, WHITE, 178);
  { const int cx = x + 34, cy = y + 106, cw = 66, ch = 48;   /* gold chip + contact lines */
    Color g0 = { 243, 223, 160 }, g1 = { 184, 145, 63 }, g2 = { 247, 231, 180 }, g3 = { 154, 118, 48 }, ln = { 80, 60, 20 };
    for (int j = 0; j < ch; j++) for (int i = 0; i < cw; i++) { float cov = rr_cov((float)(cx + i), (float)(cy + j), (float)cx, (float)cy, cw, ch, 9); if (cov <= 0) continue;
      float t = (i + j) / (float)(cw + ch); Color c = t < 0.45f ? mixc(g0, g1, t / 0.45f) : t < 0.55f ? mixc(g1, g2, (t - 0.45f) / 0.1f) : mixc(g2, g3, (t - 0.55f) / 0.45f);
      if (j == 15 || j == 16 || j == ch - 17 || j == ch - 16 || i == 30 || i == 31) c = mixc(c, ln, 0.45f);
      blend(cx + i, cy + j, c, (int)(255 * cov)); }
    rrline(cx, cy, cw, ch, 9, 2, WHITE, 89); }
  char up[48]; snprintf(up, sizeof up, "%s", S.crew[0] ? S.crew : "YOUR CREW"); for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
  textg(F.sb38, up, x + 34, bl(F.sb38, y + 206, 48), WHITE, { 185, 181, 196 }, 1, 1, 5);
  Color lc = { 207, 205, 214 };
  if (S.since[0]) { GA = 140; text(F.r12, "CREW SINCE", x + 34, bl(F.r12, y + 270, 15), lc, 1, 3); GA = 255; text(F.r20, S.since, x + 34, bl(F.r20, y + 291, 26), lc, 1, 4); }
  GA = 140; text(F.r12, "PLAYERS", x + 34 + 60 + textw(F.r12, "CREW SINCE", 3), bl(F.r12, y + 270, 15), lc, 1, 3); GA = 255;   /* the count itself: card_live */
}
/* 0.18: drawn every frame on the still card – PLAYERS n/m and, for each profile that's ready, its face fading in bottom-right (room
   profile first/left, then the players in order, overlapping); the card's edge lights up briefly each time one arrives */
struct TRows;
static void card_face(const Fonts &F, float cx, float cy, int i, const char *name, const TScr &S, int room, int a) {
  circ(cx, cy, 26, { 23, 22, 27 }, a); glow_disc(cx, cy, 23, 10, S.AC, a * 60 / 255);
  if (room) { circ(cx, cy, 23, S.AC, a); char l[2] = { S.rname[0] ? S.rname[0] : 'R', 0 }; int w = textw(F.pixs, l); GA = GA * a / 255; text(F.pixs, l, (int)cx - w / 2 + 1, (int)cy + 9, DARK); GA = 255; return; }
  static const Color AVC2[4] = { { 74, 168, 255 }, { 155, 124, 255 }, { 255, 138, 92 }, { 63, 208, 201 } };
  circ(cx, cy, 23, AVC2[(i + 3) % 4], a); char l[2] = { name[0] ? name[0] : '?', 0 }; if (l[0] >= 'a' && l[0] <= 'z') l[0] -= 32;
  int w = textw(F.pixs, l); GA = GA * a / 255; text(F.pixs, l, (int)cx - w / 2 + 1, (int)cy + 9, WHITE); GA = 255;
}
static void card_sweep(int x, int y, double t) {   /* one soft light band every 4 s */
  double ph = fmod(t, 4.0) / 4.0; if (ph < 0.4) return; float k = (float)((ph - 0.4) / 0.6); k = k < 0.5f ? 2 * k * k : 1 - 2 * (1 - k) * (1 - k);
  float bx = -0.5f * KW + k * 1.8f * KW, bw = 0.3f * KW;
  for (int j = 0; j < KH; j++) { float off = bx + (KH / 2.f - j) * 0.325f; for (int i = (int)off - 2; i < (int)(off + bw) + 2; i++) { if (i < 0 || i >= KW) continue;
    float cov = rr_cov((float)(x + i), (float)(y + j), (float)x, (float)y, KW, KH, 30); if (cov <= 0) continue;
    float q = (i - off) / bw; if (q < 0 || q > 1) continue; float a = 0.18f * (1 - fabsf(q * 2 - 1)); blend(x + i, y + j, WHITE, (int)(255 * a * cov)); } }
}
static void load_still(const Fonts &F, const TCfg &C, const TScr &S, double t) {
  float k; GA = 255; chrome(F, S);
  GA = 255; deck_L(S); spr_build_L(S);   /* 0.21 (v5): the card reader in the right half – the card drops in live (load_v5) */
  k = ease((float)((t - 0.5) / 0.5)); int dy = (int)(14 * (1 - k)); int y = heading(F, C.w_kick, S.AC, C.w_title, S.crew, 330, 620, dy, (int)(255 * k));
  char b[200]; put_crew(b, sizeof b, C.w_sub, S.crew); GA = (int)(255 * k); y = wrap(F.body, b, LX, y + 24, 560, 42, SUBC);
  if (S.line[0]) wrap(F.tiny, S.line, LX, y + 14, 560, 32, !strncmp(S.line, "Sorry", 5) ? RED : S.AC);
  GA = 255; if (S.err[0]) text(F.tiny, S.err, LX, 1056, DIM);
}

/* ---- the rows ("Who's playing in <room>"): an avatar, the name, a Waiting / Loading / Ready badge. Each row lights up in order
   as it's ready (a short room-colour flash), "Player 1" turns into the member's name, the row being loaded has a moving shine ---- */
struct TRows { int bn; char bst[5], bcur[5][32], btgt[5][32], bwho[5][32]; int lit[5]; double lit_t[5], last_lit; };
static const Color AVC[4] = { { 74, 168, 255 }, { 155, 124, 255 }, { 255, 138, 92 }, { 63, 208, 201 } };
static void badge(const Fonts &F, int xr, int cy, int kind, double t) {   /* kind 0 waiting, 1 loading, 2 ready, 3 failed, 4 clearing, 5 next */
  const char *lab = kind == 1 ? "Loading" : kind == 2 ? "Ready" : kind == 3 ? "Ask staff" : kind == 4 ? "Clearing" : kind == 5 ? "Next" : "Waiting";
  Color c = kind == 1 || kind == 4 ? YEL : kind == 2 ? GREEN : kind == 3 ? RED : LAV; int ba = kind == 0 || kind == 5 ? 89 : 115;
  int tw = textw(F.sb20, lab), w = 16 + 12 + 8 + tw + 16 + 4, x = xr - w;
  rrline(x, cy - 23, w, 46, 23, 2, c, ba);
  int da = (kind == 1 || kind == 4) ? (int)(255 * (0.25f + 0.75f * (0.5f + 0.5f * cosf((float)t * 7.85f)))) : 255; circ(x + 18 + 6, (float)cy, 6, c, da);
  text(F.sb20, lab, x + 18 + 12 + 8, bl(F.sb20, cy - 13, 26), c);
}
static void avatar(const Fonts &F, int x, int y, int i, const char *name, const TScr &S, int room) {
  float cx = x + 28.f, cy = y + 28.f;
  if (room) { circ(cx, cy, 31, S.AC, 150); circ(cx, cy, 28, S.AC, 255); char l[2] = { S.rname[0] ? S.rname[0] : 'R', 0 }; int w = textw(F.pix18, l); text(F.pix18, l, (int)cx - w / 2, (int)cy + 9, DARK); return; }
  Color c = AVC[(i + 3) % 4]; circ(cx, cy, 28, c, 255); char l[2] = { name[0] ? name[0] : '?', 0 }; if (l[0] >= 'a' && l[0] <= 'z') l[0] -= 32;
  int w = textw(F.pix18, l); text(F.pix18, l, (int)cx - w / 2, (int)cy + 9, WHITE);
}
static void draw_rows(const Fonts &F, const TCfg &C, const TScr &S, TRows &B, double t, int y0, int head) {
  GA = 255;
  if (head) { char hd[64], tpl[32]; snprintf(tpl, sizeof tpl, "%s", C.cn[2][0] ? C.cn[2] : "Who's playing in {room}"); const char *p = strstr(tpl, "{room}");
    if (p) snprintf(hd, sizeof hd, "%.*s%s%s", (int)(p - tpl), tpl, S.rname[0] ? S.rname : "this room", p + 6); else snprintf(hd, sizeof hd, "%s", tpl);
    text(F.sb24, hd, PX0, bl(F.sb24, y0 - 49, 31), SUBC); }
  for (int i = 0; i < B.bn; i++) { if (B.lit[i]) continue; char c = B.bst[i]; if ((c == 'd' || c == 'f' || c == 'u') && t - B.last_lit >= 0.45) { B.lit[i] = 1; B.lit_t[i] = t; B.last_lit = t; } break; }
  for (int i = 0; i < B.bn; i++) {
    int y = y0 + i * PRS, L = B.lit[i], active = !L && (B.bst[i] == 'c' || B.bst[i] == 'l'); double since = L ? t - B.lit_t[i] : 0; char c = B.bst[i];
    if (L && c != 'f') { rrect(PX0, y, PW, PRH, 22, GREEN, 13); rrline(PX0, y, PW, PRH, 22, 1, GREEN, 102); }   /* 0.18: a ready row tints green */
    else { rrect(PX0, y, PW, PRH, 22, WHITE, 9); rrline(PX0, y, PW, PRH, 22, 1, WHITE, 15); }
    if (L && since < 0.6) rrect(PX0, y, PW, PRH, 22, S.AC, (int)(80 * (1 - since / 0.6)));
    if (active) { double ph = fmod(t, 1.6) / 1.6; int sx = PX0 - 120 + (int)(ph * (PW + 240)); clip(PX0 + 2, y + 1, PX0 + PW - 2, y + PRH - 1);
      for (int xx = sx - 70; xx < sx + 70; xx++) { int a = 22 - abs(xx - sx) * 22 / 70; for (int yy = y + 1; yy < y + PRH - 1; yy++) blend(xx, yy, WHITE, a); } noclip(); }
    const char *nm = L ? B.btgt[i] : B.bcur[i]; int room = i == 0;
    avatar(F, PX0 + 17, y + 15, i, nm, S, room);
    int nx = PX0 + 17 + 56 + 18, by = bl(F.sb26, y + 25, 36);
    clip(nx, y + 2, PX0 + PW - 150, y + PRH - 2);
    if (L && strcmp(B.bcur[i], B.btgt[i]) && since < 0.55) { float k = (float)(since / 0.55);   /* "Player 1" turns into the member */
      GA = (int)(255 * (1 - k)); text(F.sb26, B.bcur[i], nx, by - (int)(k * 22), INK); GA = (int)(255 * k); text(F.sb26, B.btgt[i], nx, by + 22 - (int)(k * 22), INK); GA = 255; }
    else text(F.sb26, nm, nx, by, L || active ? INK : DIM);
    noclip();
    int kind = L ? (c == 'f' ? 3 : 2) : c == 'c' ? 4 : c == 'l' ? 1 : c == 'w' ? 5 : 0;
    badge(F, PX0 + PW - 17, y + PRH / 2, kind, t);
  }
}
static void card_live(const Fonts &F, const TScr &S, const TRows &B, double t) {
  int n = 0, last = -1; for (int i = 0; i < B.bn; i++) if (B.lit[i] && B.bst[i] != 'f') { n++; if (last < 0 || B.lit_t[i] > B.lit_t[last]) last = i; }
  int x = KX, y = KY; Color lc = { 207, 205, 214 };
  { char pc[16]; snprintf(pc, sizeof pc, "%d/%d", n, B.bn); GA = 255; text(F.r20, pc, x + 34 + 60 + textw(F.r12, "CREW SINCE", 3), bl(F.r20, y + 291, 26), lc, 1, 4); }
  /* faces: 46 px, 12 px overlap, the row of them ends 34 px from the right edge, 34 px above the bottom */
  float fx = (float)(x + KW - 34 - 23 - (n > 0 ? (n - 1) * 34 : 0)), fy = (float)(y + KH - 34 - 23); int k = 0;
  for (int i = 0; i < B.bn; i++) { if (!B.lit[i] || B.bst[i] == 'f') continue;
    float e = ease((float)((t - B.lit_t[i]) / 0.8)), sc = 0.85f + 0.15f * e; (void)sc;
    card_face(F, fx + k * 34, fy, i, B.btgt[i], S, i == 0, (int)(255 * e)); k++; }
  if (last >= 0 && t - B.lit_t[last] < 0.9) { float q = (float)((t - B.lit_t[last]) / 0.9), a = sinf(q * 3.14159265f);   /* the card lights up */
    rrline(x - 2, y - 2, KW + 4, KH + 4, 32, 2, S.AC, (int)(110 * a)); rrline(x - 5, y - 5, KW + 10, KH + 10, 35, 3, S.AC, (int)(40 * a)); }
}
/* the bar under the rows + "n of m profiles ready" (pmode 1 = filled to prog, 2 = a sliding piece while it gets ready) */
static void load_bar_at(const Fonts &F, const TScr &S, int x0, int y, int pw, int frame) {   /* 0.22: the older loading bar, anywhere */
  rrect(x0, y, pw, 8, 4, WHITE, 20);
  if (S.pmode == 1) { int fw = (int)(pw * S.prog); if (fw < 12) fw = 12; rrect(x0, y, fw, 8, 4, S.AC, 255);
    int sx = (frame * 6) % (fw + 120) - 120; clip(x0, y, x0 + fw, y + 8); for (int x = sx; x < sx + 120; x++) { int a = 80 - abs(x - sx - 60) * 80 / 60; for (int j = 0; j < 8; j++) blend(x0 + x, y + j, WHITE, a); } noclip(); }
  else if (S.pmode == 2) { int span = pw - 110, p = (frame * 4) % (2 * span), x = p < span ? p : 2 * span - p; rrect(x0 + x, y, 110, 8, 4, S.AC, 255); }
  if (S.plab[0]) { int w = textw(F.r22, S.plab); text(F.r22, S.plab, x0 + pw / 2 - w / 2, bl(F.r22, y + 26, 29), LAV); }
}
static void load_bar(const Fonts &F, const TScr &S, int y, int frame) {
  rrect(PX0, y, PW, 8, 4, WHITE, 20);
  if (S.pmode == 1) { int fw = (int)(PW * S.prog); if (fw < 12) fw = 12; rrect(PX0, y, fw, 8, 4, S.AC, 255);
    int sx = (frame * 6) % (fw + 120) - 120; clip(PX0, y, PX0 + fw, y + 8); for (int x = sx; x < sx + 120; x++) { int a = 80 - abs(x - sx - 60) * 80 / 60; for (int j = 0; j < 8; j++) blend(PX0 + x, y + j, WHITE, a); } noclip(); }
  else if (S.pmode == 2) { int span = PW - 110, p = (frame * 4) % (2 * span), x = p < span ? p : 2 * span - p; rrect(PX0 + x, y, 110, 8, 4, S.AC, 255); }
  if (S.plab[0]) text(F.r22, S.plab, PX0, bl(F.r22, y + 26, 29), LAV);
}

/* ---------------- 3 · ready ---------------- */
static void ready_still(const Fonts &F, const TCfg &C, const TScr &S, double t) {
  float k; GA = 255; chrome(F, S);
  k = ease((float)((t - 0.2) / 0.5)); int dy = (int)(14 * (1 - k)); int y = heading(F, C.cn[0][0] ? C.cn[0] : "SAVES LOADED", S.AC, C.w_ready, S.crew, 330, 620, dy, (int)(255 * k));
  char b[200]; put_crew(b, sizeof b, C.w_ready2, S.crew); GA = (int)(255 * k); ps_text(F, F.body, b, LX, y + 24, 560, 47, SUBC, 52, 3, F.sb16);
  GA = 255; mc_deck(S); spr_build_S(S);   /* 0.20: the save deck; 0.21: the v5 card sprite (drawn live by ready_v5) */
  GA = 255; if (S.err[0]) text(F.tiny, S.err, LX, 1056, DIM);
}
/* the orb bouncing in ("blop"), its two rings rippling out, a burst of sparkles in the room colours */
static float kf(const float *T, const float *V, int n, float t) { if (t <= T[0]) return V[0]; for (int i = 1; i < n; i++) if (t <= T[i]) return V[i - 1] + (V[i] - V[i - 1]) * (t - T[i - 1]) / (T[i] - T[i - 1]); return V[n - 1]; }
/* ================= 0.20 – Karen's save screen v4 (27 Nov 12:10pm "ok push it live"): MEMORY CARD + SAVE DECK replace the orb =========
   on Scan (1) and Ready (3). Positions = the mockup's .mcw (left 660, top 200, scale 1.2 around its 50%/55% point). The card body
   (metal, label, empty blocks, gold pins) is drawn ONCE into a sprite over the background and copied each frame at its height; the
   deck (slot, mouth, screen, LED) sits still in the background picture. Eyes, crew name, lit blocks, the deck screen's words, LED,
   sparkles, shine and "z z z" are drawn on top each frame. The card is cut at the deck's mouth line, so it slides INTO the slot. */
static const int MCX = 828, MCY = 259, MCW = 264, MCH = 348, MCCUT = 598;   /* card at rest; it is hidden below the mouth's top */
static const int SLX = 732, SLY = 607, SLW = 456, SLH = 144;                /* the save deck */
struct MCst { char crew[24], pass[64], lpass[64]; double scan_t, ins_t; };   /* scan_t/ins_t: when (tile clock) the phone scanned / swiped up; 0 = not */
static MCst g_mc;
static uint32_t *MCS = 0; static Color MCS_AC = { 1, 2, 3 };
static int pass_has(const char *pass, int i) { static const char *K[7] = { "vale", "atlas", "kai", "bella", "scarlett", "kaeden", "max" };
  if (i == 5) return 1; const char *p = pass; size_t n = strlen(K[i]); while (p && *p) { if (!strncmp(p, K[i], n) && (p[n] == ',' || !p[n])) return 1; p = strchr(p, ','); if (p) p++; } return 0; }
static int pass_n(const char *pass) { int n = 0; for (int i = 0; i < 7; i++) n += pass_has(pass, i); return n; }
static void mc_body(const TScr &S, int x, int y) {   /* the card itself, at (x, y) */
  Color a0 = { 52, 48, 60 }, a1 = { 29, 26, 35 }, a2 = { 20, 17, 24 };
  for (int j = 0; j < MCH; j++) for (int i = 0; i < MCW; i++) {
    float r = j < MCH / 2 ? 21.6f : 12.f, cov = rr_cov((float)(x + i), (float)(y + j), (float)x, (float)y, (float)MCW, (float)MCH, r); if (cov <= 0) continue;
    float t = ((i - MCW / 2.f) * 0.2588f + (j - MCH / 2.f) * 0.9659f) / 380 + 0.5f; Color c = t < 0.55f ? mixc(a0, a1, t / 0.55f) : mixc(a1, a2, (t - 0.55f) / 0.45f);
    if (j < 3) c = mixc(c, WHITE, 0.22f * (3 - j) / 3.f);
    blend(x + i, y + j, c, (int)(255 * cov)); }
  rrline(x, y, MCW, MCH, 20, 2, WHITE, 36);
  { const int lx = x + 22, ly = y + 22, lw = MCW - 44, lh = 101; Color l0 = mixc(S.AC, WHITE, 0.15f), l2 = mixc(S.AC, BLACK, 0.4f);   /* the label */
    for (int j = 0; j < lh; j++) for (int i = 0; i < lw; i++) { float cov = rr_cov((float)(lx + i), (float)(ly + j), (float)lx, (float)ly, (float)lw, (float)lh, 12); if (cov <= 0) continue;
      float t = (i + j) / (float)(lw + lh); Color c = t < 0.5f ? mixc(l0, S.AC, t / 0.5f) : mixc(S.AC, l2, (t - 0.5f) / 0.5f); if (j < 2) c = mixc(c, WHITE, 0.4f);
      blend(lx + i, ly + j, c, (int)(255 * cov)); } }
  for (int b = 0; b < 7; b++) { int bx = x + 24 + b * 32; rrect(bx, y + 146, 25, 67, 5, WHITE, 15); rrline(bx, y + 146, 25, 67, 5, 1, WHITE, 15); }   /* empty blocks */
  for (int p = 0; p < 9; p++) { int px = x + 36 + p * 22, py = y + MCH - 17 - 41;   /* gold pins */
    for (int j = 0; j < 41; j++) for (int i = 0; i < 15; i++) blend(px + i, py + j, mixc({ 201, 167, 90 }, { 140, 111, 46 }, j / 40.f), 255); }
}
static void mc_build(const TScr &S) {   /* the card sprite: background + card, once per room colour */
  if (MCS && !memcmp(&MCS_AC, &S.AC, sizeof S.AC)) return;
  if (!MCS) MCS = (uint32_t *)malloc(MCW * MCH * 4); if (!MCS) return;
  uint32_t *keep = (uint32_t *)malloc(MCW * MCH * 4); if (!keep) return;
  for (int j = 0; j < MCH; j++) memcpy(keep + j * MCW, FB + (MCY + j) * W + MCX, MCW * 4);
  mc_body(S, MCX, MCY);
  for (int j = 0; j < MCH; j++) { memcpy(MCS + j * MCW, FB + (MCY + j) * W + MCX, MCW * 4); memcpy(FB + (MCY + j) * W + MCX, keep + j * MCW, MCW * 4); }
  free(keep); MCS_AC = S.AC;
}
static void mc_blit(int dy) {   /* the card, moved dy px, cut at the mouth */
  if (!MCS) return;
  for (int j = 0; j < MCH; j++) { int Y = MCY + dy + j; if (Y < 0 || Y >= MCCUT) continue; memcpy(FB + Y * W + MCX, MCS + j * MCW, MCW * 4); }
}
/* the still parts: soft room glow behind the card, the deck with its mouth, little screen and LED, the shadow under it */
static void mc_deck(const TScr &S) {
  glow_disc(960, 487, 110, 150, S.AC, 60);
  for (int j = -30; j < 30; j++) for (int i = -210; i < 210; i++) { float ex = i / 200.f, ey = j / 22.f, d = sqrtf(ex * ex + ey * ey); if (d < 1) blend(960 + i, 769 + j, BLACK, (int)(150 * (1 - d))); }
  for (int j = 0; j < SLH; j++) for (int i = 0; i < SLW; i++) { float cov = rr_cov((float)(SLX + i), (float)(SLY + j), (float)SLX, (float)SLY, (float)SLW, (float)SLH, 24); if (cov <= 0) continue;
    Color c = mixc({ 38, 33, 44 }, { 20, 17, 24 }, j / (float)SLH); if (j < 3) c = mixc(c, WHITE, 0.12f); blend(SLX + i, SLY + j, c, (int)(255 * cov)); }
  rrline(SLX, SLY, SLW, SLH, 24, 2, WHITE, 26);
  rrect(816, MCCUT, 288, 26, 7, { 5, 4, 8 }, 255); for (int i = 816 + 7; i < 816 + 288 - 7; i++) blend(i, MCCUT + 26, WHITE, 36);
  rrect(773, 672, 276, 48, 10, { 7, 6, 10 }, 255); rrline(773, 672, 276, 48, 10, 1, WHITE, 20);
  circ(1144, 696, 8.4f, { 58, 52, 64 }, 255);
}
static void mc_eyes(float cx, float cy, int kind, float k, float look) {   /* kind 0 open, 1 sleep (lines), 2 wide, 3 happy (^ ^); k = blink (0 open … 1 shut) */
  Color ec = { 27, 13, 20 };
  for (int e = 0; e < 2; e++) { float ex = cx + (e ? 25 : -25) + look;
    if (kind == 3) { for (int j = -14; j <= 0; j++) for (int i = -14; i <= 14; i++) { float d = sqrtf((float)(i * i + j * j)); float a = d < 7 ? 0 : d < 8 ? d - 7 : d < 11.5f ? 1 : d < 12.5f ? 12.5f - d : 0; if (a > 0) blend((int)ex + i, (int)cy + 10 + j, ec, (int)(255 * a)); } continue; }
    float w = kind == 2 ? 17 : 14, h = kind == 1 ? 4 : (kind == 2 ? 29 : 24) * (1 - k) + 2.4f * k, yy = kind == 1 ? cy + 10 : cy - h / 2;
    rrect((int)(ex - w / 2), (int)yy, (int)w, (int)(h + 0.5f), (int)(h < 14 ? h / 2 : 7), ec, 255); }
}
static void mc_lcd(const Fonts &F, const char *s, Color AC, int a, int ps) {   /* the deck's little screen: pixel letters in the room colour */
  Color c = mixc(AC, WHITE, 0.2f); GA = a; int x = 790, b = bl(F.pix12, 672, 48);
  char u[40]; snprintf(u, sizeof u, "%s", s); for (char *q = u; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32; if (textw(F.pix12, u, 2) > 230) u[14] = 0;   /* capitals, cut to fit */
  int xe = text(F.pix12, u, x, b, c, 1, 2);
  if (ps) { circ((float)(xe + 22), 696.f, 14.f, c, a); circ((float)(xe + 22), 696.f, 11.5f, { 7, 6, 10 }, 255); int w = textw(F.pix12, "PS"); GA = a; text(F.pix12, "PS", xe + 22 - w / 2 + 1, 696 + 6, c); }
  GA = 255;
}
/* the card's live parts at height dy: crew name (or "MEMBER SAVE"), eyes, lit blocks with a glow, optional shine sweep */
static void mc_face(const Fonts &F, const TCfg &C, const TScr &S, int dy, int kind, float blink, float look, const char *name, int nlit, const char *pass, double shine_q, Color glowc, float glow) {
  const int cx = MCX + MCW / 2, ly = MCY + dy + 22;
  clip(0, 0, W, MCCUT);
  mc_eyes((float)cx, (float)(ly + 17 + 12), kind, blink, look);
  { char up[24]; snprintf(up, sizeof up, "%s", name); for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32; int w = textw(F.pix12, up, 2); while (w > MCW - 64 && strlen(up) > 1) { up[strlen(up) - 1] = 0; w = textw(F.pix12, up, 2); }   /* cut to fit the label */
    float lum = (S.AC.r * 0.299f + S.AC.g * 0.587f + S.AC.b * 0.114f) / 255.f; text(F.pix12, up, cx - w / 2, ly + 101 - 13, lum > 0.72f ? Color{ 27, 13, 20 } : WHITE, 1, 2); }   /* Atlas: dark letters on the near-white label */
  int lit = 0; for (int b = 0; b < 7; b++) { if (!pass_has(pass, b)) continue; if (lit >= nlit) break; lit++;
    int bx = MCX + 24 + b * 32, by = MCY + dy + 146; glow_box(bx, by, 25, 67, 5, 16, ROOMC[b], 90); rrect(bx, by, 25, 67, 5, ROOMC[b], 255); for (int i = bx + 3; i < bx + 22; i++) blend(i, by + 1, WHITE, 128); }
  if (glow > 0) rrline(MCX - 3, MCY + dy - 3, MCW + 6, MCH + 6, 23, 3, glowc, (int)(160 * glow));
  if (shine_q > 0 && shine_q < 1) { float sx = (float)(-1.2 + 2.4 * shine_q) * MCW;   /* the rainbow shine across the card */
    for (int j = 0; j < MCH; j++) { int Y = MCY + dy + j; if (Y >= MCCUT) break; for (int i = 0; i < MCW; i++) { float u = (i - sx) + j * 0.47f - MCW * 0.5f; if (u < -60 || u > 60) continue;
      Color c = u < -20 ? Color{ 255, 120, 200 } : u < 20 ? Color{ 120, 220, 255 } : Color{ 255, 240, 140 }; float a = 1 - fabsf(u) / 60.f; blend(MCX + i, Y, c, (int)(95 * a)); } } }
  noclip();
}
static float mc_bob(double now) { return -14.4f * (0.5f - 0.5f * cosf((float)(now * 6.2831853 / 3.0))); }
static void mc_sparks(double now, double tb, int b, float cx, float cy) {
  float q = (float)((now - tb) / 0.7); if (q <= 0 || q >= 1) return; float e = 1 - (1 - q) * (1 - q);
  for (int k = 0; k < 6; k++) { float a = (b * 1.7f + k * 1.047f), d = 48 * e; float px = cx + cosf(a) * d, py = cy + sinf(a) * d - 10 * e, sz = 8 * (1 - q);
    glow_disc(px, py, sz * 0.5f, 7, ROOMC[b], (int)(90 * (1 - q))); rect((int)(px - sz / 2), (int)(py - sz / 2), (int)sz + 1, (int)sz + 1, ROOMC[b]); }
}
static void mc_zz(const Fonts &F, double now) {   /* sleepy: "z z z" float up and away */
  FT_Face fz[3] = { F.pix18, F.pix22, F.pix24 };
  for (int k = 0; k < 3; k++) { float q = (float)fmod(now - k * 0.8 + 9.6, 2.4) / 2.4f; float a = q < 0.2f ? q / 0.2f * 0.9f : 0.9f * (1 - (q - 0.2f) / 0.8f);
    GA = (int)(255 * a); text(fz[k], "z", 1080 + (int)(41 * q), 240 - (int)(72 * q), { 207, 201, 222 }); } GA = 255;
}
/* 1 · Scan: waiting (bob, blink, glance at the QR, hop; sleepy after 5 s) → phone scanned (wide eyes, crew name, "HI, <CREW>!") →
   insert (on the swipe-up, or by itself 6 s after the scan): lift, glide into the slot, flash, LED, READING..., passport blocks one
   by one, SAVE FOUND n/7 (ALL 7 ROOMS! + colour cycle) + shine. Then the TV waits on this for the Load button (screen 2). */
static void scan_v4(const Fonts &F, const TCfg &C, const TScr &S, double now, double it) {
  if (it < 0.45) return;
  const MCst &M = g_mc; char hi[40], lcd[40]; int dy, kind = 0, lcda = 255, led = 0, nlit = 0, ps = 0; float blink = 0, look = 0, glow = 0; double shine = 0; Color gc = S.AC;
  const char *name = C.v_lab, *pass = M.pass[0] ? M.pass : "kaeden", *wt = C.v_wait; snprintf(lcd, sizeof lcd, "%s", C.v_insert);
  double ti0 = M.scan_t > 0 ? (M.ins_t > 0 ? (M.ins_t > M.scan_t + 0.3 ? M.ins_t : M.scan_t + 0.3) : M.scan_t + 6.0) : 0;
  if (!M.scan_t) {   /* waiting */
    int sleepy = it > 5.2; dy = (int)mc_bob(now);
    if (!sleepy) { float ph = (float)fmod(now, 5.2) / 5.2f; static const float T[] = { 0, .82f, .86f, .90f, .93f, .96f, 1 }, V[] = { 0, 0, -31, 0, -12, 0, 0 }; dy += (int)kf(T, V, 7, ph);
      blink = fmod(now, 2.6) < 0.14 ? 1.f : 0.f; double lq = fmod(now, 4.1); look = lq < 1.3 ? 11 * ease((float)(lq < 0.5 ? lq / 0.5 : lq > 0.8 ? (1.3 - lq) / 0.5 : 1)) : 0; }
    else kind = 1;
    lcda = fmod(now, 1.1) < 0.55 ? 255 : 64;
    mc_blit(dy); mc_face(F, C, S, dy, kind, blink, look, name, 0, pass, 0, gc, 0); if (sleepy) mc_zz(F, now);
  } else if (now < ti0) {   /* phone connected */
    double ts = now - M.scan_t; dy = (int)mc_bob(now); kind = ts < 0.7 ? 2 : 0; blink = ts > 0.7 && fmod(ts, 2.6) < 0.14 ? 1.f : 0.f; name = M.crew;
    put_crew(hi, sizeof hi, C.v_hi, M.crew); snprintf(lcd, sizeof lcd, "%s", hi); wt = C.v_conn;
    mc_blit(dy); mc_face(F, C, S, dy, kind, blink, 0, name, 0, pass, 0, gc, 0);
  } else {   /* inserting / inserted */
    double ti = now - ti0; name = M.crew; wt = C.v_load; float d0 = mc_bob(ti0);
    static const float T[] = { 0, .33f, 1.26f, 1.395f, 1.5f }, V0[] = { 0, -31, 120, 110, 115 }; float V[5]; for (int i = 0; i < 5; i++) V[i] = V0[i]; V[0] = d0;
    float tq = (float)ti; dy = (int)(tq < 1.5f ? (tq < .33f ? d0 + (V[1] - d0) * ease(tq / .33f) : tq < 1.26f ? V[1] + (V[2] - V[1]) * ease((tq - .33f) / .93f) : kf(T, V, 5, tq)) : 115);
    int n = pass_n(pass); double tend = 1.9 + n * 0.26 + 0.25;
    if (ti >= 1.26) { kind = 3; led = 1; int dots = (int)((ti - 1.26) / 0.3) % 4; snprintf(lcd, sizeof lcd, "%s%.*s", C.v_read, dots, "..."); }
    if (ti >= 1.9) nlit = (int)((ti - 1.9) / 0.26) + 1; if (nlit > n) nlit = n;
    if (ti >= tend) { if (n >= 7) snprintf(lcd, sizeof lcd, "%s", C.v_all); else { char nn[8]; snprintf(nn, sizeof nn, "%d", n); const char *p = strstr(C.v_found, "{n}"); if (p) snprintf(lcd, sizeof lcd, "%.*s%s%s", (int)(p - C.v_found), C.v_found, nn, p + 3); else snprintf(lcd, sizeof lcd, "%s %d/7", C.v_found, n); }
      shine = (ti - tend) / 1.3; glow = 1; if (n >= 7 && ti - tend < 3.2) gc = ROOMC[(int)((ti - tend) / 0.23) % 7]; }
    mc_blit(dy); mc_face(F, C, S, dy, kind, 0, 0, name, nlit, pass, shine, gc, glow);
    if (ti >= 1.26 && ti < 2.16) { float q = (float)((ti - 1.26) / 0.9); glow_disc(960, (float)MCCUT - 6, 70, 40, S.AC, (int)(230 * (1 - q))); }   /* the flash at the slot */
    for (int b = 0, k = 0; b < 7; b++) if (pass_has(pass, b)) { mc_sparks(now, ti0 + 1.9 + k * 0.26, b, (float)(MCX + 36 + b * 32), (float)(MCY + dy + 180)); k++; }
  }
  if (led) { glow_disc(1144, 696, 6, 14, S.AC, 200); circ(1144, 696, 8.4f, S.AC, 255); }
  mc_lcd(F, lcd, S.AC, lcda, ps);
  { int w4 = textw(F.r22, wt), x4 = QX + QS / 2 - (w4 + 60) / 2 + 60; text(F.r22, wt, x4, bl(F.r22, 768, 29), TMC); g_wtx = x4 - 60; }   /* under the QR */
}
/* 3 · Ready: the card sits in the deck, happy, its passport lit; a cheer-jump + sparkles + shine as the screen opens; the LED pulses;
   the deck screen says GAME SAVED / PRESS (PS) in turn */
static void ready_v4(const Fonts &F, const TCfg &C, const TScr &S, double it) {
  const char *pass = g_mc.lpass[0] ? g_mc.lpass : g_mc.pass[0] ? g_mc.pass : "kaeden"; char nm[24]; snprintf(nm, sizeof nm, "%.20s", S.crew[0] ? S.crew : g_mc.crew); char *pl = strstr(nm, " + "); if (pl) *pl = 0;
  static const float T[] = { 0, .225f, .45f, .63f, .9f }, V[] = { 0, -31, 0, -12, 0 }; int dy = 115 + (int)(it < 0.9 ? kf(T, V, 5, (float)it) : 0);
  mc_blit(dy); mc_face(F, C, S, dy, 3, 0, 0, nm, 7, pass, (it - 0.2) / 1.3, S.AC, 1);
  for (int b = 0, k = 0; b < 7; b++) if (pass_has(pass, b)) { mc_sparks(it, 0.1 + k * 0.08, b, (float)(MCX + 36 + b * 32), (float)(MCY + dy + 180)); k++; }
  float lp = 0.5f - 0.5f * cosf((float)(it * 6.2831853 / 1.4)); glow_disc(1144, 696, 6, 10 + 12 * lp, S.AC, (int)(200 - 60 * lp)); circ(1144, 696, 8.4f, S.AC, 255);
  int ps = fmod(it, 3.2) >= 1.6; mc_lcd(F, ps ? C.v_press : C.v_saved, S.AC, 255, ps);
}
/* ================= 0.21 – Karen's member journey v5 (27 Nov 11:20pm, owner 11:37pm "make it live") =================
   The memory card gets its real shape (rounded cut corner top-right, a notch on each side, smoky shell with a circuit board
   inside, recessed gold contacts, foil label). Scan: the deck waits EMPTY with a dashed card-shaped "your save goes here"
   outline and falling chevrons (solid + faster once a phone is connected, dim when sleepy); the deck's screen has the eyes.
   Loading: the card drops into a reader in the right half (level with "Hey, <crew>!") – one bar per profile fills
   Waiting → Loading → Ready with a tick, faces pop onto the label, a charging line climbs the card edge, sparks + a flash
   at the reader per profile, the reader's light sweeps while reading, LCD "LOADING n/N" → "ALL SET!". The list on the
   right is dropped (the card shows it). Ready: the card sits in the deck, no wiggle. Card drawn once into a sprite. */
struct Spr { uint32_t *px; int x, y, w, h, kind; Color ac; };
static Spr SPR_S = { 0, 828, 259, 264, 348, -1, { 1, 2, 3 } }, SPR_L = { 0, 1198, 270, 264, 348, -1, { 1, 2, 3 } };
static const int RDX = 1330, RDCUT = 552;   /* the loading reader: centre x, the mouth's top line */
static float mc5_cov(float i, float j, float w, float h) {   /* inside the card shape? (0..1, soft edge) */
  float r = j < h / 2 ? 14.f : 10.f, c = rr_cov(i, j, 0, 0, w, h, r); if (c <= 0) return 0;
  float d = ((w - i) + j - 36) * 0.7071f; c = fminf(c, clampf(d + 0.5f, 0, 1));   /* the cut corner (top right) */
  float n0 = h * 54 / 290, n1 = h * 104 / 290, nd = w * 7 / 220; if (j > n0 && j < n1) c = fminf(c, clampf(i - nd + 0.5f, 0, 1));   /* left notch */
  float m0 = h * 60 / 290, m1 = h * 78 / 290, md = w * 3.5f / 220; if (j > m0 && j < m1) c = fminf(c, clampf(w - md - i + 0.5f, 0, 1));   /* right notch */
  return c;
}
static void mc5_body(const TScr &S, int x, int y, int kind) {   /* kind 0: passport blocks (scan/ready), 1: profile bars (loading) */
  const int w = MCW, h = MCH; Color g0 = { 70, 64, 79 }, g1 = { 31, 27, 37 }, g2 = { 19, 16, 22 };
  for (int j = -2; j < h + 2; j++) for (int i = -2; i < w + 2; i++) {
    float cov = mc5_cov(i + 0.5f, j + 0.5f, (float)w, (float)h); if (cov <= 0) continue;
    float t = (i * 0.6f + j) / (w * 0.6f + h); Color c = t < 0.55f ? mixc(g0, g1, t / 0.55f) : mixc(g1, g2, (t - 0.55f) / 0.45f);
    float in = mc5_cov(i + 0.5f - 2, j + 0.5f - 2, (float)w - 4, (float)h - 4); if (in < 1) c = mixc(c, WHITE, 0.4f - 0.32f * j / h);   /* the edge */
    float gl = ((i - w * 0.1f) * 0.47f - (j)) ; if (gl > -30 && gl < 40 && j < h * 0.6f) c = mixc(c, WHITE, 0.08f);   /* glass glare */
    blend(x + i, y + j, c, (int)(235 * cov)); }
  { Color tr = mixc(S.AC, { 90, 83, 99 }, 0.6f);   /* the circuit board inside */
    for (int k = 0; k < 9; k++) { int x0 = x + 36 + k * 22, xm = x + 104 + k * 7; for (int j = 0; j < 10; j++) blend(x0, y + h - 54 - j, tr, 150);
      for (int j = 0; j < 14; j++) blend(x0 + (xm - x0) * j / 14, y + h - 64 - j, tr, 150); for (int j = 0; j < 6; j++) blend(xm, y + h - 78 - j, tr, 150); }
    rrect(x + w / 2 - 36, y + h - 104, 72, 19, 3, { 13, 11, 16 }, 255); rrline(x + w / 2 - 36, y + h - 104, 72, 19, 3, 1, WHITE, 40); }
  { const int lx = x + 24, ly = y + 24, lw = w - 24 - 36, lh = 96; Color l0 = mixc(S.AC, WHITE, 0.15f), l2 = mixc(S.AC, BLACK, 0.4f);   /* the label */
    for (int j = 0; j < lh; j++) for (int i = 0; i < lw; i++) { float cov = rr_cov((float)(lx + i), (float)(ly + j), (float)lx, (float)ly, (float)lw, (float)lh, 10); if (cov <= 0) continue;
      float t = (i + j) / (float)(lw + lh); Color c = t < 0.5f ? mixc(l0, S.AC, t / 0.5f) : mixc(S.AC, l2, (t - 0.5f) / 0.5f); if (j < 2) c = mixc(c, WHITE, 0.45f); if (j > lh - 3) c = mixc(c, BLACK, 0.18f);
      blend(lx + i, ly + j, c, (int)(255 * cov)); }
    rrline(lx - 3, ly - 3, lw + 6, lh + 6, 13, 3, BLACK, 64); }
  if (kind == 0) for (int b = 0; b < 7; b++) { int bx = x + 24 + b * 31; rrect(bx, y + 136, 25, 70, 5, WHITE, 15); rrline(bx, y + 136, 25, 70, 5, 1, WHITE, 15); }
  { const int px = x + 26, py = y + h - 12 - 52, pw = w - 52;   /* recessed contacts */
    rrect(px, py, pw, 52, 7, { 11, 9, 14 }, 255); for (int p = 0; p < 9; p++) { int qx = px + 8 + p * ((pw - 16 + 5) / 9), qw = (pw - 16 + 5) / 9 - 5;
      for (int j = 0; j < 46; j++) for (int i = 0; i < qw; i++) blend(qx + i, py + 6 + j, mixc({ 226, 192, 106 }, { 156, 124, 52 }, j / 45.f), 255); } }
}
static void spr_build(Spr &P, const TScr &S, int kind) {
  if (P.px && P.kind == kind && !memcmp(&P.ac, &S.AC, sizeof S.AC)) return;
  if (!P.px) P.px = (uint32_t *)malloc(P.w * P.h * 4); if (!P.px) return;
  uint32_t *keep = (uint32_t *)malloc(P.w * P.h * 4); if (!keep) return;
  for (int j = 0; j < P.h; j++) memcpy(keep + j * P.w, FB + (P.y + j) * W + P.x, P.w * 4);
  mc5_body(S, P.x, P.y, kind);
  for (int j = 0; j < P.h; j++) { memcpy(P.px + j * P.w, FB + (P.y + j) * W + P.x, P.w * 4); memcpy(FB + (P.y + j) * W + P.x, keep + j * P.w, P.w * 4); }
  free(keep); P.kind = kind; P.ac = S.AC;
}
static void spr_blit(const Spr &P, int dy, int cut) { if (!P.px) return; for (int j = 0; j < P.h; j++) { int Y = P.y + dy + j; if (Y < 0 || Y >= cut) continue; memcpy(FB + Y * W + P.x, P.px + j * P.w, P.w * 4); } }
static void deck_at(const TScr &S, int cx, int cut) {   /* a deck (reader) whose mouth's top is at y = cut, centred on cx */
  int sx = cx - 228, sy = cut + 9;
  glow_disc((float)cx, (float)cut - 120, 110, 150, S.AC, 55);
  for (int j = -30; j < 30; j++) for (int i = -210; i < 210; i++) { float ex = i / 200.f, ey = j / 22.f, d = sqrtf(ex * ex + ey * ey); if (d < 1) blend(cx + i, cut + 171 + j, BLACK, (int)(150 * (1 - d))); }
  for (int j = 0; j < SLH; j++) for (int i = 0; i < SLW; i++) { float cov = rr_cov((float)(sx + i), (float)(sy + j), (float)sx, (float)sy, (float)SLW, (float)SLH, 24); if (cov <= 0) continue;
    Color c = mixc({ 38, 33, 44 }, { 20, 17, 24 }, j / (float)SLH); if (j < 3) c = mixc(c, WHITE, 0.12f); blend(sx + i, sy + j, c, (int)(255 * cov)); }
  rrline(sx, sy, SLW, SLH, 24, 2, WHITE, 26);
  rrect(cx - 144, cut, 288, 26, 7, { 5, 4, 8 }, 255); for (int i = cx - 137; i < cx + 137; i++) blend(i, cut + 26, WHITE, 36);
  rrect(sx + 41, sy + 65, 276, 48, 10, { 7, 6, 10 }, 255); rrline(sx + 41, sy + 65, 276, 48, 10, 1, WHITE, 20);
  circ((float)(sx + 412), (float)(sy + 89), 8.4f, { 58, 52, 64 }, 255);
}
static void deck_lcd(const Fonts &F, int cx, int cut, const char *s, Color AC, int a, int eyes, int ekind, float look, int ps) {   /* eyes: small eyes first (v5) */
  int sx = cx - 228, sy = cut + 9, x = sx + 58, cy = sy + 89; Color c = mixc(AC, WHITE, 0.2f);
  if (eyes) { for (int e = 0; e < 2; e++) { int ex = x + e * 13 + (int)look; int hh = ekind == 1 ? 3 : ekind == 2 ? 16 : ekind == 3 ? 6 : ekind == 4 ? 2 : 13;
      if (ekind == 3) { for (int i = 0; i < 9; i++) blend(ex + i - 1, cy - 2 - (i == 0 || i == 8 ? 0 : i == 1 || i == 7 ? 2 : 3), c, 255); }
      else rect(ex, cy - hh / 2 + (ekind == 1 ? 4 : 0), 6, hh, c); } x += 38; }
  char u[40]; snprintf(u, sizeof u, "%s", s); for (char *q = u; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32; while (textw(F.pix12, u, 2) > (eyes ? 192 : 230) && strlen(u) > 1) u[strlen(u) - 1] = 0;
  GA = a; int xe = text(F.pix12, u, x, bl(F.pix12, sy + 65, 48), c, 1, 2);
  if (ps) { circ((float)(xe + 22), (float)cy, 14.f, c, a); circ((float)(xe + 22), (float)cy, 11.5f, { 7, 6, 10 }, 255); int w = textw(F.pix12, "PS"); text(F.pix12, "PS", xe + 22 - w / 2 + 1, cy + 6, c); }
  GA = 255;
}
static void deck_led(int cx, int cut, Color AC, float on) { int sx = cx - 228, sy = cut + 9; if (on <= 0) return; glow_disc((float)(sx + 412), (float)(sy + 89), 6, 14, AC, (int)(200 * on)); circ((float)(sx + 412), (float)(sy + 89), 8.4f, AC, (int)(255 * on)); }
/* the dashed card-shaped outline where the save goes + three falling chevrons */
static void ghost(const TScr &S, double now, int named, int sleepy) {
  const int x = SPR_S.x, y = SPR_S.y, w = MCW, h = MCH; Color c = mixc(S.AC, WHITE, 0.3f); int a = sleepy ? 64 : named ? 230 : 115;
  float off = (float)fmod(now * (named ? 0 : 27), 19.0);
  /* perimeter: walk the outline (top edge to the cut, the cut, right side, bottom, left side) */
  static float P[2600][2]; static int np = 0; if (!np) { float t;
    for (t = 14; t < w - 36; t += 1) { P[np][0] = t; P[np][1] = 0; np++; }
    for (t = 0; t < 36 * 1.41f; t += 1) { P[np][0] = w - 36 + t / 1.41f; P[np][1] = t / 1.41f; np++; }
    for (t = 36; t < h - 10; t += 1) { P[np][0] = (float)w; P[np][1] = t; np++; }
    for (t = w - 10; t > 10; t -= 1) { P[np][0] = t; P[np][1] = (float)h; np++; }
    for (t = h - 10; t > 14; t -= 1) { P[np][0] = 0; P[np][1] = t; np++; } }
  for (int k = 0; k < np; k++) { if (!named && fmod(k + off, 19.0) > 10) continue; circ(x + P[k][0], y + P[k][1], 1.4f, c, a); }
  if (!sleepy) { double sp = named ? 1.0 : 1.8;
    for (int k = 0; k < 3; k++) { double q = fmod(now - k * 0.3 * sp / 1.8 + 10, sp) / sp; float al = q < 0.4 ? (float)(q / 0.4) * 0.8f : 0.8f * (float)(1 - (q - 0.4) / 0.6); int cy = y + h / 2 - 30 + k * 26 + (int)(-10 + 24 * q);
      for (int i = -22; i <= 22; i++) { int yy = cy - abs(i) * 14 / 22; for (int tt = -3; tt <= 3; tt++) blend(x + w / 2 + i, yy + tt, c, (int)(255 * al * (1 - abs(tt) / 4.f))); } } }
}
static void scan_v5(const Fonts &F, const TCfg &C, const TScr &S, double now, double it) {
  if (it < 0.45) return;
  const MCst &M = g_mc; int named = M.scan_t > 0, sleepy = !named && it > 5.2; char lcd[40]; snprintf(lcd, sizeof lcd, "%s", C.v_insert);
  float br = 0.5f - 0.5f * cosf((float)(now * 6.2831853 / 2.6)); glow_box(816, MCCUT, 288, 26, 7, 16, S.AC, (int)(30 + 60 * br));   /* the slot breathes */
  ghost(S, now, named, sleepy);
  int ek = sleepy ? 1 : 0; float look = 0; if (!sleepy && !named) { if (fmod(now, 2.6) < 0.14) ek = 4; double lq = fmod(now, 4.1); if (lq < 1.3) look = 4; }
  const char *wt = C.v_wait;
  if (named) { double ts = now - M.scan_t; ek = ts < 0.7 ? 2 : (fmod(ts, 2.6) < 0.14 ? 4 : 0); put_crew(lcd, sizeof lcd, C.v_hi, M.crew); wt = C.v_conn;
    if (ts < 0.5) glow_box(816, MCCUT, 288, 26, 7, 26, S.AC, (int)(200 * (1 - ts / 0.5))); }
  deck_lcd(F, 960, MCCUT, lcd, S.AC, named || sleepy || fmod(now, 1.1) < 0.55 ? 255 : 64, 1, ek, look, 0);
  { int w4 = textw(F.r22, wt), x4 = QX + QS / 2 - (w4 + 60) / 2 + 60; text(F.r22, wt, x4, bl(F.r22, 768, 29), TMC); g_wtx = x4 - 60; }
}
/* the label's live parts on a card blitted at (P.x, P.y + dy): eyes, name, faces (loading), foil sheen */
static void mc5_label(const Fonts &F, const TScr &S, const Spr &P, int dy, int kind, float blink, const char *name, int cut) {
  const int lx = P.x + 24, ly = P.y + dy + 24, lw = MCW - 60; clip(0, 0, W, cut);
  mc_eyes((float)(lx + lw / 2), (float)(ly + 26), kind, blink, 0);
  { char up[24]; snprintf(up, sizeof up, "%s", name); for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32; int w = textw(F.pix12, up, 2); while (w > lw - 20 && strlen(up) > 1) { up[strlen(up) - 1] = 0; w = textw(F.pix12, up, 2); }
    float lum = (S.AC.r * 0.299f + S.AC.g * 0.587f + S.AC.b * 0.114f) / 255.f; text(F.pix12, up, lx + lw / 2 - w / 2, ly + 96 - 14, lum > 0.72f ? Color{ 27, 13, 20 } : WHITE, 1, 2); }
  noclip();
}
/* 2 · Loading: the card drops into the reader (0.7 s, a little bounce), bars fill per profile, faces pop on the label */
static double g_lit[5]; static char g_litc[5];
static void load_v5(const Fonts &F, const TCfg &C, const TScr &S, const TRows &B, double now, double it) {
  static const float T[] = { 0, .55f, .7f, .8f, .9f }, V[] = { -560, 8, -6, 2, 0 }; int dy = it < 0.9 ? (int)kf(T, V, 5, (float)it) : 0;
  static double last_it = 1e9; if (it < last_it) memset(g_litc, 0, sizeof g_litc); last_it = it;   /* a new Loading screen */
  int n = B.bn > 5 ? 5 : B.bn, nd = 0, nl = 0; for (int i = 0; i < n; i++) { char c = B.bst[i]; if (c == 'd' && g_litc[i] != 'd') g_lit[i] = now; g_litc[i] = c; if (c == 'd') nd++; if (c == 'l') nl = 1; }
  spr_blit(SPR_L, dy, RDCUT);
  int allset = n > 0 && nd == n;
  mc5_label(F, S, SPR_L, dy, allset ? 3 : 0, fmod(now, 2.6) < 0.14 ? 1.f : 0, S.crew[0] ? S.crew : g_mc.crew, RDCUT);
  clip(0, 0, W, RDCUT);
  { int fx = SPR_L.x + MCW / 2 - (nd - 1) * 13 - 13;   /* faces pop onto the label as each is ready */
    for (int i = 0, k = 0; i < n; i++) if (B.bst[i] == 'd') { float e = ease((float)((now - g_lit[i]) / 0.35)); float r = 11 * (0.4f + 0.6f * e);
      Color cc = i == 0 ? S.AC : AVC[(i + 3) % 4]; circ((float)(fx + k * 26 + 13), (float)(SPR_L.y + dy + 24 + 60), r + 2, { 23, 22, 27 }, (int)(255 * e)); circ((float)(fx + k * 26 + 13), (float)(SPR_L.y + dy + 24 + 60), r, cc, (int)(255 * e)); k++; } }
  if (n == 0) {   /* 0.22: no rows from the board – one bar for the crew, filled by the whole load */
    int by = SPR_L.y + dy + 130, bx = SPR_L.x + 28, tx = bx + 28, tw = MCW - 56 - 28 - 26; circ((float)(bx + 10), (float)(by + 10), 10, S.AC, 255);
    char l[2] = { (S.crew[0] ? S.crew[0] : (g_mc.crew[0] ? g_mc.crew[0] : 'R')), 0 }; if (l[0] >= 'a' && l[0] <= 'z') l[0] -= 32; int lw2 = textw(F.pix12, l); text(F.pix12, l, bx + 10 - lw2 / 2 + 1, by + 16, DARK);
    rrect(tx, by + 5, tw, 10, 5, WHITE, 20); float f = S.pmode == 1 ? S.prog : (float)(0.5 - 0.5 * cos(now * 2.0)) * 0.6f; if (f > 0.02f) rrect(tx, by + 5, (int)(tw * f), 10, 5, S.AC, 255); }
  for (int i = 0; i < n; i++) {   /* one bar per profile */
    int by = SPR_L.y + dy + 130 + i * 24, bx = SPR_L.x + 28; char c = B.bst[i]; Color cc = i == 0 ? S.AC : AVC[(i + 3) % 4];
    circ((float)(bx + 10), (float)(by + 10), 10, cc, 255); char l[2] = { i == 0 ? (S.rname[0] ? S.rname[0] : 'R') : (B.btgt[i][0] ? B.btgt[i][0] : '?'), 0 }; if (l[0] >= 'a' && l[0] <= 'z') l[0] -= 32;
    int lw2 = textw(F.pix12, l); text(F.pix12, l, bx + 10 - lw2 / 2 + 1, by + 16, i == 0 ? DARK : WHITE);
    int tx = bx + 28, tw = MCW - 56 - 28 - 26; rrect(tx, by + 5, tw, 10, 5, WHITE, 20);
    float f = c == 'd' ? 1.f : c == 'l' ? (float)(0.15 + 0.7 * (0.5 - 0.5 * cos(fmod(now - g_lit[i], 3.0) * 1.047))) : 0.f; if (c == 'l' && S.prog > 0) f = fmaxf(f, 0.2f);
    if (f > 0) { rrect(tx, by + 5, (int)(tw * f), 10, 5, cc, 255); glow_box(tx, by + 5, (int)(tw * f), 10, 5, 8, cc, 70); }
    if (c == 'd') { int kx = tx + tw + 8, ky = by + 10; for (int q = 0; q < 6; q++) rect(kx + q, ky + q / 2, 2, 2, GREEN); for (int q = 0; q < 10; q++) rect(kx + 5 + q, ky + 3 - q, 2, 2, GREEN); }
    if (c == 'f') text(F.pix12, "!", tx + tw + 8, by + 16, RED); }
  { float pr = n ? (float)nd / n : 0; int ex = SPR_L.x + MCW - 4, y1 = SPR_L.y + dy + MCH - 60, y0 = y1 - (int)((MCH - 100) * pr);   /* the charging line */
    for (int y = y0; y < y1; y++) { blend(ex, y, S.AC, 255); blend(ex - 1, y, S.AC, 160); for (int g = 2; g < 9; g++) blend(ex - g, y, S.AC, 60 - g * 6); } }
  noclip();
  for (int i = 0; i < n; i++) if (B.bst[i] == 'd') { mc_sparks(now, g_lit[i], i % 7, (float)(SPR_L.x + MCW - 30), (float)(SPR_L.y + dy + 140 + i * 24));
    if (now - g_lit[i] < 0.6) glow_box(RDX - 144, RDCUT, 288, 26, 7, 22, S.AC, (int)(200 * (1 - (now - g_lit[i]) / 0.6))); }
  if (it >= 0.55 && it < 1.3) glow_box(RDX - 144, RDCUT, 288, 26, 7, 30, S.AC, (int)(220 * (1 - (it - 0.55) / 0.75)));   /* the reader lights as the card lands */
  if (nl && !allset) { float q = (float)fmod(now, 1.4) / 1.4f; int sx = RDX - 228 + (int)(456 * q); for (int i = -40; i < 40; i++) blend(sx + i, RDCUT + 9 + 1, S.AC, (int)(160 * (1 - fabsf(i / 40.f))));   /* light sweep */ }
  deck_led(RDX, RDCUT, S.AC, allset ? 1.f : nl ? (fmod(now, 0.3) < 0.15 ? 1.f : 0.2f) : 0.5f);
  /* 0.22 (Karen: keep the older loading bar instead of the LCD LOADING n/N): the reader's screen just reads, the bar + words sit under it */
  char lc[32]; if (allset) snprintf(lc, sizeof lc, "ALL SET!"); else { int dots = (int)(now / 0.3) % 4; snprintf(lc, sizeof lc, "%s%.*s", C.v_read, dots, "..."); }
  deck_lcd(F, RDX, RDCUT, lc, S.AC, 255, 1, allset ? 3 : (fmod(now, 2.6) < 0.14 ? 4 : 0), 0, 0);
  load_bar_at(F, S, RDX - 228, RDCUT + 9 + SLH + 42, 456, (int)(now * 60));
  if (allset) glow_disc((float)RDX, (float)(SPR_L.y + MCH / 2), 140, 120, S.AC, 40);
}
/* 3 · Ready: the v5 card in the deck, happy, passport lit (no wiggle) */
static void ready_v5(const Fonts &F, const TCfg &C, const TScr &S, double it) {
  const char *pass = g_mc.lpass[0] ? g_mc.lpass : g_mc.pass[0] ? g_mc.pass : "kaeden"; char nm[24]; snprintf(nm, sizeof nm, "%.20s", S.crew[0] ? S.crew : g_mc.crew); char *pl = strstr(nm, " + "); if (pl) *pl = 0;
  const int dy = 115; spr_blit(SPR_S, dy, MCCUT); mc5_label(F, S, SPR_S, dy, 3, 0, nm, MCCUT);
  clip(0, 0, W, MCCUT); for (int b = 0; b < 7; b++) if (pass_has(pass, b)) { int bx = SPR_S.x + 24 + b * 31, by = SPR_S.y + dy + 136; glow_box(bx, by, 25, 70, 5, 16, ROOMC[b], 90); rrect(bx, by, 25, 70, 5, ROOMC[b], 255); for (int i = bx + 3; i < bx + 22; i++) blend(i, by + 1, WHITE, 128); } noclip();
  float lp = 0.5f - 0.5f * cosf((float)(it * 6.2831853 / 1.4)); glow_disc(1144, 696, 6, 10 + 12 * lp, S.AC, (int)(200 - 60 * lp)); circ(1144, 696, 8.4f, S.AC, 255);
  int ps = fmod(it, 3.2) >= 1.6; deck_lcd(F, 960, MCCUT, ps ? C.v_press : C.v_saved, S.AC, 255, 1, 3, 0, ps);
  glow_disc(960, (float)(SPR_S.y + dy + 100), 140, 120, S.AC, 35);
}
static void spr_build_S(const TScr &S) { spr_build(SPR_S, S, 0); }
static void spr_build_L(const TScr &S) { spr_build(SPR_L, S, 1); }
static void deck_L(const TScr &S) { deck_at(S, RDX, RDCUT); }
static void ready_live(const TScr &S, double t) {
  static const float T[] = { 0, .1f, .2f, .33f, .43f, .58f, 1 }, TY[] = { 0, -30, 4, -18, 2, 0, 0 }, SX[] = { 1, .9f, 1.15f, .94f, 1.08f, .98f, 1 }, SY[] = { 1, 1.12f, .85f, 1.07f, .92f, 1.02f, 1 };
  float bt = (float)(t / 1.4); float ty = bt < 1 ? kf(T, TY, 7, bt) : 0, sx = bt < 1 ? kf(T, SX, 7, bt) : 1, sy = bt < 1 ? kf(T, SY, 7, bt) : 1;
  Color rc = mixc(S.AC, WHITE, 0.3f);
  for (int k = 0; k < 2; k++) { double q = fmod(t + (k ? 1.1 : 0), 2.2) / 2.2; if (t < (k ? 1.1 : 0)) continue; float e = 1 - (1 - (float)q) * (1 - (float)q); annulus(OCX, OCY, ORB_R * (1 + 0.6f * e) - 3, 3, rc, (int)(178 * (1 - q))); }
  orb(OCX, OCY + ty, ORB_R, sx, sy, 1, S.AC);
  if (t > 0.25 && t < 2.3) { unsigned s = 12345;
    for (int j = 0; j < 40; j++) { s = s * 1103515245u + 12345u; float a = (s >> 8) % 6283 / 1000.f; s = s * 1103515245u + 12345u; float d = 200 + (s >> 8) % 160; s = s * 1103515245u + 12345u; float dur = 1.2f + ((s >> 8) % 700) / 1000.f;
      float q = (float)((t - 0.25) / dur); if (q >= 1) continue; float e = 1 - (1 - q) * (1 - q) * (1 - q), sc = q < 0.05f ? q / 0.05f : 1 - (q - 0.05f) / 0.95f, op = q < 0.05f ? q / 0.05f : 1 - (q - 0.05f) / 0.95f;
      Color c = j % 3 ? S.AC : ROOMC[j % 7]; float px = OCX + cosf(a) * d * e, py = OCY + sinf(a) * d * e;
      glow_disc(px, py, 4 * sc, 10, c, (int)(120 * op)); circ(px, py, 6 * sc, c, (int)(255 * op)); } }
}

/* ---------------- 4 · what happens next ---------------- */
static void next_still(const Fonts &F, const TCfg &C, const TScr &S) {
  GA = 255; chrome(F, S); heading(F, C.n_title, S.AC, C.cn[1][0] ? C.cn[1] : "Ready, player one.", S.crew, 340, 700);
}
static int step_h(const TCfg &C, int i) { const char *s = C.n[i]; if (!s[0]) return 0; int ph = (strstr(s, "[PS]") ? 52 : 42) + (strchr(s, '|') ? 35 : 0); return (ph > 66 ? ph : 66) + 45; }
static void next_live(const Fonts &F, const TCfg &C, const TScr &S, double t, double left, double total) {
  int y = SY0;
  for (int i = 0; i < 4; i++) { int hgt = step_h(C, i); if (!hgt) continue;
    float k = ease((float)((t - 0.3 - i * 0.25) / 0.5)); if (k <= 0) { y += hgt; continue; } GA = (int)(255 * k); int dx = (int)(30 * (1 - k));
    char main_[104], small_[104] = ""; const char *s = C.n[i]; if (s[0] >= '1' && s[0] <= '9' && s[1] == '.') { s += 2; while (*s == ' ') s++; }
    const char *bar = strchr(s, '|'); if (bar) { snprintf(main_, sizeof main_, "%.*s", (int)(bar - s), s); snprintf(small_, sizeof small_, "%s", bar + 1); } else snprintf(main_, sizeof main_, "%s", s);
    int mh = strstr(main_, "[PS]") ? 52 : 42, ph = mh + (small_[0] ? 35 : 0), ch = ph > 66 ? ph : 66, cy = y + 22 + ch / 2;
    marble(SX0 + dx + 33.f, (float)cy, 33, S.AC, 255, 1); char dg[2] = { (char)('1' + i), 0 }; int dw = textw(F.pix22, dg); text(F.pix22, dg, SX0 + dx + 33 - dw / 2 + 1, cy + 11, DARK);
    int ty = cy - ph / 2; ps_text(F, F.r32, main_, SX0 + 96 + dx, ty, SW - 96, mh, INK, 52, 3, F.sb16);
    if (small_[0]) text(F.tiny, small_, SX0 + 96 + dx, bl(F.tiny, ty + mh + 4, 31), LAV);
    GA = (int)(18 * k); recta(SX0 + dx, y + hgt - 1, SW, 1, WHITE, 255); GA = 255;
    y += hgt; }
  /* 0.21 (Karen v5 10:55pm: bigger, clearer "Closing in"): a glowing deck-screen box centred at the bottom – thick blocks run out,
     then CLOSING */
  { const int bw = 480, bh = 70, bx = 960 - bw / 2, by = 950; float frac = (float)(left / total); if (frac < 0) frac = 0;
    glow_box(bx, by, bw, bh, 16, 22, S.AC, 70); rrect(bx, by, bw, bh, 16, { 7, 6, 10 }, 255); rrline(bx, by, bw, bh, 16, 2, S.AC, 200);
    int nb = 6, on = (int)ceilf(frac * nb); for (int i = 0; i < nb; i++) { int qx = bx + 26 + i * 24; if (i < on) { glow_box(qx, by + 17, 16, 36, 3, 8, S.AC, 90); rrect(qx, by + 17, 16, 36, 3, S.AC, 255); } else rrect(qx, by + 17, 16, 36, 3, WHITE, 24); }
    char cl[32]; int n = (int)left + 1; if (left <= 0.05) snprintf(cl, sizeof cl, "CLOSING"); else snprintf(cl, sizeof cl, "CLOSING %d", n);
    text(F.pix24, cl, bx + 26 + nb * 24 + 18, bl(F.pix24, by, bh), mixc(S.AC, WHITE, 0.25f), 1, 3); }
}
#endif
