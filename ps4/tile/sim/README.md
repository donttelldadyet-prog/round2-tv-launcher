# Tile simulator (desktop)

Draws the Round2 tile's screens with the exact drawing code the PS4 uses (`../tile/draw.h`), into PPM pictures – for checking a
layout before building the PS4 package, and for the design preview.

    cd ps4/tile/sim
    gcc -c -O2 ../tile/qrcodegen.c -I../tile -o qr.o
    g++ -O2 -I../tile $(pkg-config --cflags freetype2) sim.cpp qr.o $(pkg-config --libs freetype2) -o sim && ./sim

Writes scan0–5.ppm (the scan screen's intro and still picture), scanreset.ppm, signed0–3.ppm and signeddone.ppm.
