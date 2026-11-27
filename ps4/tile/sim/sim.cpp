/* Tile simulator: draws the tile's screens with the exact drawing code the PS4 uses (../tile/draw.h) into PPM pictures.
   ROOM=<vale|kaeden|…> picks the room colour (default kaeden). Writes scan_*.ppm, load_*.ppm, ready_*.ppm, next_*.ppm. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ft2build.h>
#include FT_FREETYPE_H
struct Color { uint8_t r, g, b; };
extern "C" {
#include "qrcodegen.h"
}
#include "draw.h"
#define FD "../assets/fonts/"
static FT_Library lib;
static FT_Face ld(const char *f, int px, void *) { char p[256]; snprintf(p, sizeof p, FD "%s", f); FT_Face fc; if (FT_New_Face(lib, p, 0, &fc)) return NULL; FT_Set_Pixel_Sizes(fc, 0, px); return fc; }
static void save(const char *fn) { FILE *f = fopen(fn, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; i++) { uint32_t p = FB[i]; fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f); } fclose(f); }
int main() {
  FT_Init_FreeType(&lib);
  Fonts F; fonts_load(F, ld, NULL);
  TCfg C; cfg_default(&C);
  const char *rm = getenv("ROOM") ? getenv("ROOM") : "kaeden";
  struct { const char *k, *n; Color c; } R[] = { { "vale", "Vale", { 0xE8, 0x43, 0x6A } }, { "kaeden", "Kaeden", { 0x4D, 0xB8, 0x48 } }, { "kai", "Kai", { 0x24, 0xB4, 0x9A } }, { "bella", "Bella", { 0x7A, 0x5B, 0xE0 } },
    { "scarlett", "Scarlett", { 0xF2, 0xC2, 0x1B } }, { "atlas", "Atlas", { 0xE9, 0xE7, 0xEF } }, { "maximillion", "Maximillion", { 0xE0, 0x40, 0x2F } } };
  TScr S; memset(&S, 0, sizeof S); for (auto &r : R) if (!strcmp(r.k, rm)) { strcpy(S.room, r.k); strcpy(S.rname, r.n); S.AC = r.c; }
  strcpy(S.url, "https://book.round2.sg/m?room=kaeden&k=Ab12Cd34&v=2"); strcpy(S.st, "ready");
  uint32_t *BG0 = (uint32_t *)malloc(W * H * 4), *BUF = (uint32_t *)malloc(W * H * 4); FB = BUF; bg_v2(BG0, S.AC);
  char fn[64];
  double ts[] = { 0.3, 0.8, 1.3, 3, 5.2 };
  for (int i = 0; i < 5; i++) { memcpy(BUF, BG0, W * H * 4); scan_still(F, C, S, ts[i]); scan_live(S, ts[i], ease((float)((ts[i] - 0.4) / 0.8))); scan_v4(F, C, S, 100 + ts[i], ts[i]); snprintf(fn, sizeof fn, "scan_%d.ppm", i); save(fn); }
  /* 0.20: v4 card states – sleepy, phone connected, inserting, blocks lighting, inserted (PASS env = passport, default 4 rooms) */
  { const char *pp = getenv("PASS") ? getenv("PASS") : "vale,kai,kaeden,max"; strcpy(g_mc.pass, pp); strcpy(g_mc.crew, getenv("CREW") ? getenv("CREW") : "Night Owls");
    double nows[] = { 99, 100.3, 101.2, 102.5, 103.3, 105 }; g_mc.scan_t = 0;
    memcpy(BUF, BG0, W * H * 4); scan_still(F, C, S, 99); scan_live(S, 107.3, 1); scan_v4(F, C, S, 107.3, 9); save("v4_0sleepy.ppm");
    g_mc.scan_t = 100; g_mc.ins_t = 0;
    for (int i = 0; i < 6; i++) { if (i >= 2) g_mc.ins_t = 100.5; memcpy(BUF, BG0, W * H * 4); scan_still(F, C, S, 99); scan_live(S, nows[i], 1); scan_v4(F, C, S, nows[i] + (i >= 2 ? 0 : 0), 9); snprintf(fn, sizeof fn, "v4_%d.ppm", i + 1); save(fn); }
    g_mc.scan_t = 0; }
  strcpy(S.st, "reset"); strcpy(S.line, "This room is being reset (3 of 7 games). Member saves load right after."); memcpy(BUF, BG0, W * H * 4); scan_still(F, C, S, 3); scan_live(S, 3); save("scan_reset.ppm");
  /* loading */
  strcpy(S.st, "loading"); S.busy = 1; strcpy(S.crew, "Night Owls"); strcpy(S.since, "11/26"); S.line[0] = 0;
  TRows B; memset(&B, 0, sizeof B); B.bn = 3; const char *cur[] = { "Kaeden profile", "Player 1", "Player 2" }, *tg[] = { "Kaeden profile", "Jay", "Kim" }, *who[] = { "Night Owls", "Jay", "Kim" };
  for (int i = 0; i < 3; i++) { strcpy(B.bcur[i], cur[i]); strcpy(B.btgt[i], tg[i]); strcpy(B.bwho[i], who[i]); }
  const char *sts[] = { "lww", "dlw", "ddl" }; float pr[] = { 0.15f, 0.5f, 0.8f };
  for (int s = 0; s < 3; s++) { for (int i = 0; i < 3; i++) B.bst[i] = sts[s][i]; memset(B.lit, 0, sizeof B.lit); B.last_lit = -9; double tt = 10;
    for (int i = 0; i < 3; i++) if (B.bst[i] == 'd') { B.lit[i] = 1; B.lit_t[i] = 0; }
    S.pmode = 1; S.prog = pr[s]; snprintf(S.plab, sizeof S.plab, "%d of 3 profiles ready", s);
    memcpy(BUF, BG0, W * H * 4); load_still(F, C, S, 3); card_sweep(KX, KY, 3.2); draw_rows(F, C, S, B, tt, 389, 1); card_live(F, S, B, tt); load_bar(F, S, 389 + 3 * PRS - 12 + 22, 40);
    snprintf(fn, sizeof fn, "load_%d.ppm", s); save(fn); }
  /* five rows */
  B.bn = 5; const char *c5[] = { "Kaeden profile", "Karen", "Ronnie", "Player 3", "Player 4" }; for (int i = 0; i < 5; i++) { strcpy(B.bcur[i], c5[i]); strcpy(B.btgt[i], c5[i]); B.lit[i] = 0; }
  strcpy(B.bst, "dlww"); B.bst[4] = 'w'; B.lit[0] = 1; S.prog = 0.3f; strcpy(S.plab, "1 of 3 profiles ready");
  memcpy(BUF, BG0, W * H * 4); load_still(F, C, S, 3); draw_rows(F, C, S, B, 10, 389, 1); load_bar(F, S, 389 + 5 * PRS - 12 + 22, 40); save("load_5rows.ppm");
  /* ready */
  S.done = 1; S.busy = 0; B.bn = 3; for (int i = 0; i < 3; i++) { strcpy(B.bcur[i], cur[i]); strcpy(B.btgt[i], tg[i]); B.bst[i] = 'd'; B.lit[i] = 1; B.lit_t[i] = -5; }
  double tr[] = { 0.15, 0.3, 0.6, 1.0, 2.6 };
  for (int i = 0; i < 5; i++) { memcpy(BUF, BG0, W * H * 4); ready_still(F, C, S, tr[i]); ready_v4(F, C, S, tr[i]); draw_rows(F, C, S, B, 30, 400, 0); snprintf(fn, sizeof fn, "ready_%d.ppm", i); save(fn); }
  /* next */
  double tn[] = { 0.5, 1.0, 2.5 };
  for (int i = 0; i < 3; i++) { memcpy(BUF, BG0, W * H * 4); next_still(F, C, S); next_live(F, C, S, tn[i], 6 - tn[i], 6); snprintf(fn, sizeof fn, "next_%d.ppm", i); save(fn); }
  return 0;
}
