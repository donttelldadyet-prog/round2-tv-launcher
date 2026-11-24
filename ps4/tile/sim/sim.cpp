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
static FT_Face ld(const char *f, int px) { FT_Face fc; FT_New_Face(lib, f, 0, &fc); FT_Set_Pixel_Sizes(fc, 0, px); return fc; }
static void save(const char *fn) { FILE *f = fopen(fn, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; i++) { uint32_t p = FB[i]; fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f); } fclose(f); }
int main() {
  FT_Init_FreeType(&lib);
  Fonts F = { ld(FD "ChakraPetch-SemiBold.ttf", 72), ld(FD "ChakraPetch-Regular.ttf", 44), ld(FD "ChakraPetch-SemiBold.ttf", 34), ld(FD "ChakraPetch-Regular.ttf", 24), ld(FD "PressStart2P-Regular.ttf", 64), ld(FD "PressStart2P-Regular.ttf", 20) };
  TCfg C; cfg_default(&C);
  TScr S; memset(&S, 0, sizeof S); strcpy(S.room, "kaeden"); strcpy(S.rname, "Kaeden"); strcpy(S.url, "https://book.round2.sg/m?room=kaeden&k=Ab12Cd34&v=2"); strcpy(S.st, "ready"); S.AC = { 0x4D, 0xB8, 0x48 };
  uint32_t *BG0 = (uint32_t *)malloc(W * H * 4), *BUF = (uint32_t *)malloc(W * H * 4); FB = BUF;
  Glow g1[2] = { { SCX + 240, SCY + 180, 640, 0.32f }, { QX + QS / 2, QY + QS / 2, 380, 0.16f } }; bg_compose(BG0, S.AC, g1, 2);
  double ts[] = { 0.25, 0.6, 0.9, 1.25, 1.6, 3 }; char fn[64];
  for (int i = 0; i < 6; i++) { memcpy(BUF, BG0, W * H * 4); draw_scan(F, C, S, ts[i]); snprintf(fn, sizeof fn, "scan%d.ppm", i); save(fn); }
  /* reset status on the scan screen */
  strcpy(S.st, "reset"); strcpy(S.line, "This room is being reset (3 of 7 games). Member saves load right after."); memcpy(BUF, BG0, W * H * 4); draw_scan(F, C, S, 3); save("scanreset.ppm");
  /* signed in */
  strcpy(S.st, "loading"); S.busy = 1; strcpy(S.crew, "NIGHT OWLS"); strcpy(S.since, "11/26"); strcpy(S.line, "Loading Karen's saves (player 2 of 3).");
  Glow g2[1] = { { WCX + WCW / 2, WCY + WCH / 2, 700, 0.30f } }; bg_compose(BG0, S.AC, g2, 1);
  TRows B; memset(&B, 0, sizeof B); B.bn = 5; const char *st = "dldww"; const char *cur[] = { "KAEDEN", "Player 1", "Player 2", "Player 3", "Player 4" }, *tg[] = { "KAEDEN", "Karen", "Ronnie", "Ash", "Player 4" }, *who[] = { "Night Owls", "Karen", "Ronnie", "Ash", "" };
  for (int i = 0; i < 5; i++) { B.bst[i] = st[i]; strcpy(B.bcur[i], cur[i]); strcpy(B.btgt[i], tg[i]); strcpy(B.bwho[i], who[i]); }
  B.bst[4] = 'u';
  double ts2[] = { 0.3, 0.9, 1.4, 2.2 };
  for (int i = 0; i < 4; i++) { memcpy(BUF, BG0, W * H * 4); draw_signed(F, C, S, ts2[i]); B.lit[0] = 1; B.lit_t[0] = -5; B.lit[2] = 0; draw_rows(F, S.AC, B, 10 + ts2[i]);
    rect(LX, 930, 1090, 22, CARD); rect(LX, 930, 1090 * 4 / 10, 22, S.AC); text(F.small, "1 of 3 profiles ready  -  40%", LX, 1000, INK); snprintf(fn, sizeof fn, "signed%d.ppm", i); save(fn); }
  S.done = 1; S.busy = 0; memcpy(BUF, BG0, W * H * 4); draw_signed(F, C, S, 3); for (int i = 0; i < 5; i++) { B.bst[i] = 'd'; B.lit[i] = 1; B.lit_t[i] = -5; } B.bst[4] = 'u'; draw_rows(F, S.AC, B, 30); save("signeddone.ppm");
  return 0;
}
