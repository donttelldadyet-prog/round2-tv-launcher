"""Download the Google Fonts the menu uses and point the page at the local copies."""
import re, sys, urllib.request, os

page = sys.argv[1]
html = open(page, encoding="utf-8").read()
m = re.search(r'<link href="(https://fonts.googleapis.com/css2[^"]+)" rel="stylesheet">', html)
if not m:
    sys.exit(0)
ua = {"User-Agent": "Mozilla/5.0 (Linux; Android 11) AppleWebKit/537.36 Chrome/110 Safari/537.36"}
css = urllib.request.urlopen(urllib.request.Request(m.group(1).replace("&amp;", "&"), headers=ua), timeout=30).read().decode()
outdir = os.path.join(os.path.dirname(page), "fonts")
os.makedirs(outdir, exist_ok=True)
for i, url in enumerate(dict.fromkeys(re.findall(r"url\((https://fonts.gstatic.com/[^)]+)\)", css))):
    name = "f%d.woff2" % i
    with open(os.path.join(outdir, name), "wb") as f:
        f.write(urllib.request.urlopen(urllib.request.Request(url, headers=ua), timeout=30).read())
    css = css.replace(url, "fonts/" + name)
html = html.replace(m.group(0), "<style>\n" + css + "\n</style>")
html = re.sub(r'<link rel="preconnect"[^>]*>\n?', "", html)
open(page, "w", encoding="utf-8").write(html)
print("fonts bundled:", len(os.listdir(outdir)))
