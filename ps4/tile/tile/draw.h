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
  w_kick[32], w_title[64], w_sub[120], w_ready[64], w_ready2[96], n_title[48], n[5][104]; float t_burst, t_next; };
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
  h1, h1r, sb38, sb30, sb26, sb24, sb20, sb16, sb15, r32, r26, r22, r20, r17, r12, pix24, pix22, pix18; };
typedef FT_Face (*FontLoad)(const char *file, int px, void *ctx);
static void fonts_load(Fonts &F, FontLoad L, void *x) {
  const char *SB = "ChakraPetch-SemiBold.ttf", *RG = "ChakraPetch-Regular.ttf", *PX = "PressStart2P-Regular.ttf";
  F.big = L(SB, 72, x); F.mid = L(RG, 44, x); F.small = L(SB, 34, x); F.tiny = L(RG, 24, x); F.pix = L(PX, 64, x); F.pixs = L(PX, 20, x); F.body = L(RG, 30, x);
  F.h1 = L(SB, 76, x); F.h1r = L(RG, 76, x); F.sb38 = L(SB, 38, x); F.sb30 = L(SB, 30, x); F.sb26 = L(SB, 26, x); F.sb24 = L(SB, 24, x); F.sb20 = L(SB, 20, x); F.sb16 = L(SB, 16, x); F.sb15 = L(SB, 15, x);
  F.r32 = L(RG, 32, x); F.r26 = L(RG, 26, x); F.r22 = L(RG, 22, x); F.r20 = L(RG, 20, x); F.r17 = L(RG, 17, x); F.r12 = L(RG, 12, x);
  F.pix24 = L(PX, 24, x); F.pix22 = L(PX, 22, x); F.pix18 = L(PX, 18, x);
}

/* ---- screen state (what the engine + the helper said) ---- */
struct TScr { char room[32], rname[32], url[200], crew[48], since[8], valid[8], title[80], line[200], line2[200], err[96], st[24], plab[96]; Color AC; int busy, done; float prog; int pmode; };

/* positions (mockup, 1920x1080) */
static const int LX = 130, RX = 1790, OCX = 960, OCY = 570, ORB_R = 165, ARC_R = 260, QX = 1440, QY = 330, QS = 300;
static const int KX = 748, KY = 389, KW = 560, KH = 353;                 /* the crew card (0.18: v3 – between the words and the list, top = first row) */
static const int PX0 = 1370, PW = 420, PRH = 86, PRS = 98;               /* the rows */
static const int SX0 = 820, SY0 = 300, SW = 980;                         /* the steps */

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
  k = ease((float)((t - 0.6) / 0.6)); GA = (int)(235 * k); arc_text(F.pix22, C.c[0], OCX, OCY, ARC_R, INK, 255);
  k = ease((float)((t - 0.8) / 0.35)); GA = (int)(255 * k);
  if (S.url[0]) draw_qr(S.url, QX, QY, QS); else { rrect(QX, QY, QS, QS, 18, CARD, 255); text(F.tiny, "QR code appears when", QX + 40, QY + 140, DIM); text(F.tiny, "Round2 answers", QX + 70, QY + 176, DIM); }
  GA = 255; if (t > 0.95) corners(QX, QY, QS, AC, 255, (float)((t - 0.95) / 0.25));
  k = ease((float)((t - 1.0) / 0.4)); GA = (int)(255 * k);
  { int w1 = textw(F.sb30, C.s_qr1); text(F.sb30, C.s_qr1, QX + QS / 2 - w1 / 2, bl(F.sb30, 670, 39), INK);
    const char *to = NULL, *q = C.s_qr2; while ((q = strstr(q, " to ")) != NULL) { to = q; q += 4; }   /* "New here? Scan to " + "create one" in the room colour */
    int w2 = textw(F.tiny, C.s_qr2), x2 = QX + QS / 2 - w2 / 2, b2 = bl(F.tiny, 719, 31);
    if (to) { char a[48]; snprintf(a, sizeof a, "%.*s", (int)(to - C.s_qr2) + 4, C.s_qr2); int xe = text(F.tiny, a, x2, b2, LAV); text(F.tiny, to + 4, xe, b2, AC); }
    else text(F.tiny, C.s_qr2, x2, b2, LAV);
    if (S.url[0]) { const char *wt = "Waiting for your phone"; int w4 = textw(F.r22, wt), x4 = QX + QS / 2 - (w4 + 60) / 2 + 60; text(F.r22, wt, x4, bl(F.r22, 768, 29), TMC); g_wtx = x4 - 60; } }   /* 0.18: dots in scan_live */
  k = ease((float)((t - 1.1) / 0.4)); GA = (int)(255 * k);
  ps_text(F, F.r26, C.s_ps, LX, 962, 1000, 46, LAV, 46, 2, F.sb15);
  { int w3 = textw(F.tiny, C.s_url, 1); text(F.tiny, C.s_url, RX - w3, bl(F.tiny, 969, 31), URLC, 1, 1); }
  GA = 255; if (S.err[0]) text(F.tiny, S.err, LX, 1056, DIM);
}
/* moving parts: the glass orb with the 7 room marbles orbiting it (those behind it go first, dimmer and smaller) */
static void scan_live(const TScr &S, double t, float fade = 1) {
  int wtx = g_wtx; if (S.url[0] && wtx > 0) for (int d = 0; d < 3; d++) {   /* 0.18: three dots pulsing in turn before "Waiting for your phone" */
    float ph = (float)fmod(t - d * 0.2, 1.2) / 1.2f; if (ph < 0) ph += 1; float k = 0.5f - 0.5f * cosf(ph * 6.2831853f);
    GA = (int)(255 * fade); circ((float)(wtx + 5 + d * 20), 783.f, 5 * (0.7f + 0.3f * k), S.AC, (int)(255 * (0.25f + 0.75f * k))); GA = 255; } const float tl = -10 * 3.14159265f / 180; float px[7], py[7], sc[7], op[7], dz[7];
  for (int i = 0; i < 7; i++) { float th = i * 6.2831853f / 7 + (float)t * 0.35f, ex = cosf(th) * 270, ey = sinf(th) * 120;
    px[i] = OCX + ex * cosf(tl) - ey * sinf(tl); py[i] = OCY + ex * sinf(tl) + ey * cosf(tl) + sinf((float)t * 3 + i * 6.2831853f / 7 * 3) * 8;
    dz[i] = sinf(th); float f = (dz[i] + 1) / 2; sc[i] = 0.7f + 0.5f * f; op[i] = 0.45f + 0.55f * f; }
  int ga = (int)(255 * fade);
  for (int i = 0; i < 7; i++) if (dz[i] <= 0) { GA = ga; marble(px[i], py[i], 22 * sc[i], ROOMC[i], (int)(255 * op[i]), 1); }
  GA = ga; orb(OCX, OCY, ORB_R, 1, 1, 0, S.AC);
  for (int i = 0; i < 7; i++) if (dz[i] > 0) { GA = ga; marble(px[i], py[i], 22 * sc[i], ROOMC[i], (int)(255 * op[i]), 1); }
  GA = 255;
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
  k = ease((float)(t / 0.7)); GA = (int)(255 * k); crew_card(F, C, S, KX, KY + (int)(40 * (1 - k)));
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
  glow_disc(OCX, OCY, ORB_R - 10, 130, S.AC, 120);   /* the full orb's glow */
  k = ease((float)((t - 0.2) / 0.5)); int dy = (int)(14 * (1 - k)); int y = heading(F, C.cn[0][0] ? C.cn[0] : "SAVES LOADED", S.AC, C.w_ready, S.crew, 330, 620, dy, (int)(255 * k));
  char b[200]; put_crew(b, sizeof b, C.w_ready2, S.crew); GA = (int)(255 * k); ps_text(F, F.body, b, LX, y + 24, 560, 47, SUBC, 52, 3, F.sb16);
  k = ease((float)((t - 0.3) / 0.6)); GA = (int)(235 * k); arc_text(F.pix22, C.c[1], OCX, OCY, ARC_R, INK, 255);
  GA = 255; if (S.err[0]) text(F.tiny, S.err, LX, 1056, DIM);
}
/* the orb bouncing in ("blop"), its two rings rippling out, a burst of sparkles in the room colours */
static float kf(const float *T, const float *V, int n, float t) { if (t <= T[0]) return V[0]; for (int i = 1; i < n; i++) if (t <= T[i]) return V[i - 1] + (V[i] - V[i - 1]) * (t - T[i - 1]) / (T[i] - T[i - 1]); return V[n - 1]; }
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
  /* "Closing in N" with a ring that empties clockwise from the top */
  int cx = 1622 + 20, cy = 986; float frac = (float)(left / total); if (frac < 0) frac = 0;
  annulus((float)cx, (float)cy, 14, 4, WHITE, 31);
  for (int yy = cy - 19; yy <= cy + 19; yy++) for (int xx = cx - 19; xx <= cx + 19; xx++) { float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy, d = sqrtf(dx * dx + dy * dy); float cov = clampf(fminf(d - 14 + 0.5f, 18 - d + 0.5f), 0, 1); if (cov <= 0) continue;
    float ang = atan2f(dx, -dy); if (ang < 0) ang += 6.2831853f; if (ang <= frac * 6.2831853f) blend(xx, yy, S.AC, (int)(255 * cov)); }
  char cl[32]; int n = (int)left + 1; if (left <= 0.05) snprintf(cl, sizeof cl, "Closing"); else snprintf(cl, sizeof cl, "Closing in %d", n);
  text(F.r22, cl, 1676, bl(F.r22, 971, 29), TMC);
}
#endif
