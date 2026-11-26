# Tile simulator (desktop)

Draws the Round2 tile's screens with the exact drawing code the PS4 uses (`../tile/draw.h`), into PPM pictures – for checking a
layout before building the PS4 package, and for the design preview.

    cd ps4/tile/sim
    gcc -c -O2 ../tile/qrcodegen.c -I../tile -o qr.o
    g++ -O2 -I../tile $(pkg-config --cflags freetype2) sim.cpp qr.o $(pkg-config --libs freetype2) -o sim && ROOM=vale ./sim

`ROOM` = vale, kaeden (default), kai, bella, scarlett, atlas or maximillion. Writes (tile 0.18, Karen's v3 screens):
scan_0–4.ppm (scan screen intro → still), scan_reset.ppm, load_0–2.ppm (loading, 0/1/2 profiles ready), load_5rows.ppm,
ready_0–4.ppm (ready: bounce, sparkles, rings), next_0–2.ppm (what happens next, steps sliding in).
