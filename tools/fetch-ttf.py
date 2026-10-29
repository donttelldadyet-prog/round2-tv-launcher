"""Saves the menu's fonts as TTF (web-assets/fonts-native/) for the pop-up cards drawn outside the menu:
the Android card over other apps (Typeface from assets) and Round2 Alerts on LG. Google serves TTF to a plain client."""
import os, re, sys, urllib.request
out = sys.argv[1] if len(sys.argv) > 1 else "web-assets/fonts-native"
os.makedirs(out, exist_ok=True)
want = {"chakra-500.ttf": ("Chakra+Petch", "500"), "chakra-700.ttf": ("Chakra+Petch", "700"), "pixel.ttf": ("Press+Start+2P", "400")}
for name, (fam, w) in want.items():
    css = urllib.request.urlopen(f"https://fonts.googleapis.com/css?family={fam}:{w}", timeout=30).read().decode()
    url = re.search(r"url\((https://fonts.gstatic.com/[^)]+)\)", css).group(1)
    open(os.path.join(out, name), "wb").write(urllib.request.urlopen(url, timeout=30).read())
    print("font", name, os.path.getsize(os.path.join(out, name)))
