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

"loop": "blend" makes any clip loop seamlessly: the last 1.5 s are cross-faded into the start, so AI
videos can be made with free, visible motion instead of "same first and last frame" (which tends to
come out almost still).

Ambience sounds ("sounds": [{"id", "name", "source", "rooms"}]) are made into seamless loops for TVs:
loudness-matched to the built-in rain, the end blended into the start so there's no gap or click,
saved as <id>.ogg (up to 3 minutes). A theme can name one as its "ambience".
"""
import array, hashlib, json, math, os, re, subprocess, sys, urllib.request

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

def blend_loop(src_path, dest):
    """cross-fade the clip's last X seconds into its first X: plays end-to-start with no jump"""
    dur = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", src_path],
                               check=True, capture_output=True, text=True).stdout.strip() or 0)
    dur = min(dur, 60.0)
    if dur < 3:
        return src_path
    x = round(min(1.5, dur / 4), 3)
    f = (f"[0:v]fps=24,settb=1/24,split=2[a][b];"
         f"[a]trim=start={x}:end={dur},setpts=PTS-STARTPTS[main];"
         f"[b]trim=start=0:end={x},setpts=PTS-STARTPTS[head];"
         f"[main][head]xfade=transition=fade:duration={x}:offset={round(dur - 2 * x, 3)},format=yuv420p[v]")
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", src_path, "-filter_complex", f, "-map", "[v]",
                    "-an", "-c:v", "libx264", "-preset", "veryfast", "-crf", "12", dest], check=True)
    return dest

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
    entry = {k: t[k] for k in ("name", "rooms", "from", "until", "takeover", "layout", "cards", "focus", "ambience") if k in t}
    if entry.get("cards") not in (None, "right", "low", "top", "middle", "bottom"):
        sys.exit(f"Theme '{tid}': cards must be right, low, top, middle or bottom")
    entry["id"] = tid
    entry["src"] = source if source.startswith("http") else source + ":" + sha(os.path.join("themes", source))
    blend = t.get("loop") == "blend"
    if blend:
        entry["src"] += "|blend"
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
        if blend:
            raw = blend_loop(raw, os.path.join(OUT, tid + ".blend.mp4"))
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", raw, "-filter_complex", video_filter(z, x, y), "-map", "[v]",
                        "-an", "-c:v", "libx264", "-preset", "slow", "-crf", "21", "-profile:v", "main", "-level", "4.0", "-g", "24",
                        "-keyint_min", "24", "-sc_threshold", "0", "-bf", "2", "-pix_fmt", "yuv420p", "-t", "60", "-movflags", "+faststart", mp4], check=True)
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-ss", "0.5", "-i", mp4, "-frames:v", "1",
                        "-vf", "scale=320:-2", "-q:v", "3", jpg], check=True)
        entry.update(video=BASE + tid + ".mp4", thumb=BASE + tid + ".jpg", sha256=sha(mp4), bytes=os.path.getsize(mp4))
        print(f"{tid}: encoded {entry['bytes'] // 1024} KB")
    pub.append(entry)

# ---------------- ambience sounds ----------------
def fetch(source, dest):
    if source.startswith("http"):
        req = urllib.request.Request(source, headers={"User-Agent": "Round2Home"})
        with urllib.request.urlopen(req) as r, open(dest, "wb") as f:
            f.write(r.read())
        return dest
    return os.path.join("themes", source)

def make_loop(raw, ogg, sr=44100):
    """decode -> loudness-match -> blend the last 1.5 s into the start -> OGG"""
    pcm = subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-i", raw, "-t", "180", "-vn",
                          "-af", "loudnorm=I=-26:TP=-3:LRA=11", "-ac", "2", "-ar", str(sr), "-f", "f32le", "-"],
                         check=True, capture_output=True).stdout
    a = array.array("f"); a.frombytes(pcm)
    frames = len(a) // 2
    if frames < sr * 2:
        sys.exit(f"{raw}: the sound is shorter than 2 seconds")
    x = min(int(sr * 1.5), frames // 4)
    out = array.array("f", a[: (frames - x) * 2])
    for i in range(x):   # equal-power crossfade: the loop's end flows into its start
        t = i / x
        gi, go = math.sin(t * math.pi / 2), math.cos(t * math.pi / 2)
        for ch in (0, 1):
            out[2 * i + ch] = a[2 * i + ch] * gi + a[2 * (frames - x + i) + ch] * go
    tmp = ogg + ".raw"
    with open(tmp, "wb") as f:
        f.write(out.tobytes())
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-f", "f32le", "-ar", str(sr), "-ac", "2", "-i", tmp,
                    "-c:a", "libvorbis", "-q:a", "4", ogg], check=True)
    os.remove(tmp)

try:
    old_sounds = {t["id"]: t for t in json.load(open(os.path.join(OUT, "previous.json"))).get("sounds", [])}
except Exception:
    old_sounds = {}
sounds = []
sound_ids = set()
for snd in src.get("sounds", []):
    sid = snd.get("id", "")
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,40}", sid) or sid in ("rain", "storm", "off"):
        sys.exit(f"Sound id '{sid}' must be lowercase letters, numbers and dashes (and not rain/storm/off)")
    source = snd.get("source", "")
    if not source:
        sys.exit(f"Sound '{sid}' has no source (an audio link, or a file in the themes folder)")
    e = {k: snd[k] for k in ("name", "rooms") if k in snd}
    e["id"] = sid
    e["src"] = source if source.startswith("http") else source + ":" + sha(os.path.join("themes", source))
    prev = old_sounds.get(sid)
    if prev and prev.get("src") == e["src"] and prev.get("pcm"):
        for k in ("audio", "sha256", "bytes", "pcm", "pcmSha256", "pcmBytes"):
            e[k] = prev[k]
        keep.extend([sid + ".ogg", sid + ".wav"])
        print(f"sound {sid}: unchanged")
    else:
        ogg = os.path.join(OUT, sid + ".ogg")
        make_loop(fetch(source, os.path.join(OUT, sid + ".src")), ogg)
        e.update(audio=BASE + sid + ".ogg", sha256=sha(ogg), bytes=os.path.getsize(ogg))
        # LG TVs: plain 16-bit WAV the menu unpacks itself (playing the OGG there takes the video player away)
        wav = os.path.join(OUT, sid + ".wav")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", ogg, "-ac", "2", "-ar", "22050",
                        "-c:a", "pcm_s16le", "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", wav], check=True)
        e.update(pcm=BASE + sid + ".wav", pcmSha256=sha(wav), pcmBytes=os.path.getsize(wav))
        print(f"sound {sid}: {e['bytes'] // 1024} KB loop, {e['pcmBytes'] // 1024} KB WAV for LG")
    sound_ids.add(sid)
    sounds.append(e)
for t in pub:
    amb = t.get("ambience")
    if amb and amb not in ("rain", "storm", "off") and amb not in sound_ids:
        sys.exit(f"Theme '{t['id']}' uses the ambience '{amb}', which isn't in sounds")

out = {"themes": pub}
if sounds:
    out["sounds"] = sounds
staff = src.get("staff") or {}
if staff:   # staff PIN (stored only as a SHA-256) and whether guests may change the background
    if staff.get("pinHash") and not re.fullmatch(r"[0-9a-f]{64}", staff["pinHash"]):
        sys.exit("staff.pinHash must be a SHA-256 hex string")
    if "pinLength" in staff and not (isinstance(staff["pinLength"], int) and 4 <= staff["pinLength"] <= 8):
        sys.exit("staff.pinLength must be 4-8")
    out["staff"] = {k: staff[k] for k in ("pinHash", "pinLength", "guestBackground") if k in staff}
# Room defaults (Theme manager › Rooms › Default settings): each TV's normal look and sound
ROOMS_OK = {"vale", "kaeden", "scarlett", "bella", "kai", "atlas", "maximillion"}
ALLOWED = {"wall": r"[a-z0-9-]{1,50}", "amb": r"[a-z0-9-]{1,41}", "ambVol": r"([1-9]|10)", "sound": r"[a-z]{2,10}",
           "fx": r"[a-z]{2,10}", "entry": r"[a-z]{2,12}", "cards": r"[a-z]{2,8}", "updated": r"[0-9TZ:.\-]{10,40}"}
rd_out = {}
for room, d in (src.get("roomDefaults") or {}).items():
    if room not in ROOMS_OK or not isinstance(d, dict):
        sys.exit(f"roomDefaults: unknown room '{room}'")
    clean = {}
    for k, v in d.items():
        if k in ALLOWED and v is not None and re.fullmatch(ALLOWED[k], str(v)):
            clean[k] = str(v)
    if clean:
        rd_out[room] = clean
if rd_out:
    out["roomDefaults"] = rd_out
app = src.get("app") or {}
if app.get("updateRequested"):   # Theme manager › Settings › Ask TVs to update
    out["app"] = {"updateRequested": str(app["updateRequested"])[:40]}
json.dump(out, open(os.path.join(OUT, "themes.json"), "w"), indent=1)
with open(os.path.join(OUT, "keep.txt"), "w") as f:
    f.write("\n".join(keep))
print(f"{len(pub)} theme(s) ready")
