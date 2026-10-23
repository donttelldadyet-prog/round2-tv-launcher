#!/usr/bin/env python3
"""Publish event backgrounds for the Round2 TVs.

Reads themes/themes.json (what staff edit), encodes each theme's video for TVs
(1080p H.264, no sound, 24 fps), makes a thumbnail, and writes out/ with:
  <id>.mp4, <id>.jpg, themes.json (what the TVs read: dates, rooms, download links, SHA-256)
Videos already published from the same source and framing are reused, not re-encoded.

Framing ("frame": {"zoom": 1.2, "x": 0.5, "y": 0.4}) is how the Theme manager lets staff adjust a
video without making a new one: zoom 1 fills the TV screen (cropping what doesn't fit), above 1 zooms
in, below 1 shrinks it over a blurred copy of itself; x/y (0-1) slide it left-right / up-down.
Any shape of video works (a phone video in portrait too).
"""
import hashlib, json, os, re, subprocess, sys, urllib.request

REPO = os.environ.get("GITHUB_REPOSITORY", "donttelldadyet-prog/round2-tv-launcher")
BASE = f"https://github.com/{REPO}/releases/download/themes/"
OUT = "out"
os.makedirs(OUT, exist_ok=True)

src = json.load(open("themes/themes.json"))
try:
    old = {t["id"]: t for t in json.load(open(os.path.join(OUT, "previous.json"))).get("themes", [])}
except Exception:
    old = {}

def framing(t):
    f = t.get("frame") or {}
    try:
        z = min(3.0, max(0.25, float(f.get("zoom", 1))))
        x = min(1.0, max(0.0, float(f.get("x", 0.5))))
        y = min(1.0, max(0.0, float(f.get("y", 0.5))))
    except (TypeError, ValueError):
        sys.exit(f"Theme '{t.get('id')}': frame zoom/x/y must be numbers")
    return round(z, 3), round(x, 3), round(y, 3)

def video_filter(z, x, y):
    cover = "max(1920/iw\\,1080/ih)"
    return (f"[0:v]fps=24,split=2[a][b];"
            f"[a]scale=1920:1080:force_original_aspect_ratio=increase,crop=1920:1080,gblur=sigma=40,eq=brightness=-0.12[bg];"
            f"[b]scale=w='ceil({cover}*iw*{z}/2)*2':h='ceil({cover}*ih*{z}/2)*2'[fg];"
            f"[bg][fg]overlay=x='(W-w)*{x}':y='(H-h)*{y}',format=yuv420p[v]")

def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()

pub, keep = [], []
for t in src.get("themes", []):
    tid = t.get("id", "")
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,40}", tid):
        sys.exit(f"Theme id '{tid}' must be lowercase letters, numbers and dashes")
    source = t.get("source", "")
    if not source:
        sys.exit(f"Theme '{tid}' has no source (a video link, or a file in the themes folder)")
    entry = {k: t[k] for k in ("name", "rooms", "from", "until", "takeover", "layout", "cards", "focus") if k in t}
    if entry.get("cards") not in (None, "right", "low", "top", "middle", "bottom"):
        sys.exit(f"Theme '{tid}': cards must be right, low, top, middle or bottom")
    entry["id"] = tid
    entry["src"] = source if source.startswith("http") else source + ":" + sha(os.path.join("themes", source))
    z, x, y = framing(t)
    if (z, x, y) != (1, 0.5, 0.5):
        entry["src"] += f"|zoom={z},x={x},y={y}"
    prev = old.get(tid)
    if prev and prev.get("src") == entry["src"]:
        for k in ("video", "thumb", "sha256", "bytes"):
            entry[k] = prev[k]
        keep.append(tid)
        print(f"{tid}: unchanged, keeping published video")
    else:
        raw = os.path.join(OUT, tid + ".src")
        if source.startswith("http"):
            req = urllib.request.Request(source, headers={"User-Agent": "Round2Home"})
            with urllib.request.urlopen(req) as r, open(raw, "wb") as f:
                f.write(r.read())
        else:
            raw = os.path.join("themes", source)
        mp4, jpg = os.path.join(OUT, tid + ".mp4"), os.path.join(OUT, tid + ".jpg")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", raw, "-filter_complex", video_filter(z, x, y), "-map", "[v]",
                        "-an", "-c:v", "libx264", "-preset", "slow", "-crf", "21", "-profile:v", "main", "-level", "4.0", "-g", "24",
                        "-keyint_min", "24", "-sc_threshold", "0", "-bf", "2", "-pix_fmt", "yuv420p", "-t", "60", "-movflags", "+faststart", mp4], check=True)
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-ss", "0.5", "-i", mp4, "-frames:v", "1",
                        "-vf", "scale=320:-2", "-q:v", "3", jpg], check=True)
        entry.update(video=BASE + tid + ".mp4", thumb=BASE + tid + ".jpg", sha256=sha(mp4), bytes=os.path.getsize(mp4))
        print(f"{tid}: encoded {entry['bytes'] // 1024} KB")
    pub.append(entry)

out = {"themes": pub}
staff = src.get("staff") or {}
if staff:   # staff PIN (stored only as a SHA-256) and whether guests may change the background
    if staff.get("pinHash") and not re.fullmatch(r"[0-9a-f]{64}", staff["pinHash"]):
        sys.exit("staff.pinHash must be a SHA-256 hex string")
    if "pinLength" in staff and not (isinstance(staff["pinLength"], int) and 4 <= staff["pinLength"] <= 8):
        sys.exit("staff.pinLength must be 4-8")
    out["staff"] = {k: staff[k] for k in ("pinHash", "pinLength", "guestBackground") if k in staff}
app = src.get("app") or {}
if app.get("updateRequested"):   # Theme manager › Settings › Ask TVs to update
    out["app"] = {"updateRequested": str(app["updateRequested"])[:40]}
json.dump(out, open(os.path.join(OUT, "themes.json"), "w"), indent=1)
with open(os.path.join(OUT, "keep.txt"), "w") as f:
    f.write("\n".join(keep))
print(f"{len(pub)} theme(s) ready")
