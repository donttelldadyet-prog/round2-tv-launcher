// Round2 – iPhone widgets (Scriptable app). Made by the control centre › Tests & systems › Phone alerts & widgets (20 Nov 2026).
// Home screen: hold an empty spot → Edit → Add widget → Scriptable → pick a size → tap the new widget → Script: Round2.
// Lock screen: hold the lock screen → Customize → Lock Screen → tap the widget row → Scriptable → tap it → Script: Round2.
// "Parameter" on a widget (optional, words can be combined, e.g. "tv nonames"):
//   (empty)   rooms board: who's in till when, what's on the TV and PS4, next TV message
//   tv        what each room's TV is showing and which PS4 game is running
//   msgs      TV messages coming up (10-minute warnings, wrap-ups, guest messages), in time order
//   times     tonight's start and end times, in time order
//   room:Kai  just one room, in detail          nonames   hide guest names
// Data refreshes every few minutes (iOS decides exactly when). Tap a widget to open the control centre.
const KEY = '__KEY__', API = '__URL__', CC = 'https://staff.round2.sg/';
const RC = { Vale: '#E8436A', Kaeden: '#4DB848', Scarlett: '#F2C21B', Bella: '#7A5BE0', Kai: '#24B49A', Atlas: '#E9E7EF', Max: '#E0402F' };
const BG = new Color('#0b0914'), INK = new Color('#f4f1fb'), MUTE = new Color('#8d84a6'), ACC = new Color('#F2C21B'), WARN = new Color('#ff9f43'), OK = new Color('#4DB848');
const fam = config.widgetFamily || 'large';
const par = String(args.widgetParameter || '').toLowerCase();
const lock = /^accessory/.test(fam), names = !/nonames/.test(par) && !lock;
const only = (par.match(/room:\s*([a-z]+)/) || [])[1];
const view = /\btv\b/.test(par) ? 'tv' : /\bmsgs?\b|notif/.test(par) ? 'msgs' : /\btimes?\b/.test(par) ? 'times' : 'board';
const big = fam === 'large' || fam === 'extraLarge';

/* Round2's own pixel icons (the same ones as the booking page) – each room's character icon, plus TV, PS4 pad, bell, cake … */
const PIX = {"tv":["..#.....#...","...#...#....","....#.#.....","############","#..........#","#...#......#","#...###....#","#...#####..#","#...###....#","#...#......#","#..........#","############"],"clock":["...######...","..#......#..",".#...#....#.","#....#.....#","#....#.....#","#....###...#","#.......#..#","#........#.#","#..........#",".#........#.","..#......#..","...######..."],"user":["....####....","...#....#...","...#....#...","...#....#...","....####....","............","..########..",".#........#.","#..........#","#..........#","#..........#","############"],"cake":[".....##.....","......#.....",".....##.....",".....##.....",".##########.",".#........#.",".#.##..##.#.",".##########.","#..........#","#.#.#..#.#.#","#..........#","############"],"cat":["............",".#.......#..",".##.....##..",".#.#####.#..",".#.......#..","#..#...#..#.","#.........#.","#....#....#.",".#..#.#..#..","..#.....#...","...#####....","............"],"mount":["............","............",".....#......","....#.#.....","...#...#.#..","..#.#.#.#.#.","..#..#..#..#",".#.........#",".#.........#","#...........","############","............"],"book":["............",".####..####.","#....##....#","#.##.##.##.#","#....##....#","#.##.##.##.#","#....##....#","#.##.##.##.#","#....##....#",".####..####.",".....##.....","............"],"boat":[".....#......",".....##.....",".....###....",".....####...",".....#####..",".....######.",".....#......","############",".##########.","..########..","............","#.##.##.##.#"],"car":["............","............","...######...","..#..#...#..",".#...#....#.","############","#..........#","#.##....##.#","############",".##......##.",".##......##.","............"],"saturn":["..............","..............",".....####.....","....######..##","...#########.#","...#######.##.","...#####..#...","...##...###...",".##..######...","#.#########...","##..######....",".....####.....","..............",".............."],"leaf":[".........###","......######","....#####..#","...####...#.","..####...#..","..###...#...",".###...#....",".##...#.....",".#.###......",".###........","#...........","............"],"moon":["....####....","..###.......",".##.........",".#..........","##..........","##..........","##..........","##.........#",".##.......##",".###.....##.","..########..","....####...."],"pad":["............","..########..",".#........#.","#..#....#..#","#.###..#.#.#","#..#....#..#","#..........#","#...####...#",".#.#....#.#.","..#......#..","............","............"],"bell":[".....##.....","....####....","...######...","...######...","..########..","..########..",".##########.","############","............",".....##.....","............","............"],"warn":[".....##.....","....####....","....#..#....","...##..##...","...##..##...","..###..###..","..###..###..",".##########.",".####..####.","#####..#####","############","............"],"check":["............","...........#","..........##",".........##.","#.......##..","##.....##...",".##...##....","..##.##.....","...###......","....#.......","............","............"],"sparkle":["......#.....","......#.....",".....###....","###########.",".....###....","......#.....","..#...#.....","..#.........",".###......#.","..#......###","..#.......#.","............"]};
/* Round2's look (owner 20 Nov: "design it with Round2 design principles and main theme"): the save-slot screen of the website –
   dark violet-black, the pixel font for labels (ROUND2, SLOT SELECT), the yellow segmented bar, square frames with the
   bright corner brackets, each room's own colour and character icon. Press Start 2P isn't on iPhones, so labels are drawn
   with this 5×7 pixel alphabet. */
const PFONT = {"A":[".###.","#...#","#...#","#####","#...#","#...#","#...#"],"B":["####.","#...#","#...#","####.","#...#","#...#","####."],"C":[".####","#....","#....","#....","#....","#....",".####"],"D":["####.","#...#","#...#","#...#","#...#","#...#","####."],"E":["#####","#....","#....","####.","#....","#....","#####"],"F":["#####","#....","#....","####.","#....","#....","#...."],"G":[".####","#....","#....","#.###","#...#","#...#",".####"],"H":["#...#","#...#","#...#","#####","#...#","#...#","#...#"],"I":["#####","..#..","..#..","..#..","..#..","..#..","#####"],"J":["..###","...#.","...#.","...#.","...#.","#..#.",".##.."],"K":["#...#","#..#.","#.#..","##...","#.#..","#..#.","#...#"],"L":["#....","#....","#....","#....","#....","#....","#####"],"M":["#...#","##.##","#.#.#","#.#.#","#...#","#...#","#...#"],"N":["#...#","##..#","#.#.#","#..##","#...#","#...#","#...#"],"O":[".###.","#...#","#...#","#...#","#...#","#...#",".###."],"P":["####.","#...#","#...#","####.","#....","#....","#...."],"Q":[".###.","#...#","#...#","#...#","#.#.#","#..#.",".##.#"],"R":["####.","#...#","#...#","####.","#.#..","#..#.","#...#"],"S":[".####","#....","#....",".###.","....#","....#","####."],"T":["#####","..#..","..#..","..#..","..#..","..#..","..#.."],"U":["#...#","#...#","#...#","#...#","#...#","#...#",".###."],"V":["#...#","#...#","#...#","#...#","#...#",".#.#.","..#.."],"W":["#...#","#...#","#...#","#.#.#","#.#.#","##.##","#...#"],"X":["#...#","#...#",".#.#.","..#..",".#.#.","#...#","#...#"],"Y":["#...#","#...#",".#.#.","..#..","..#..","..#..","..#.."],"Z":["#####","....#","...#.","..#..",".#...","#....","#####"],"0":[".###.","#...#","#..##","#.#.#","##..#","#...#",".###."],"1":["..#..",".##..","..#..","..#..","..#..","..#..",".###."],"2":[".###.","#...#","....#","...#.","..#..",".#...","#####"],"3":["####.","....#","....#",".###.","....#","....#","####."],"4":["...#.","..##.",".#.#.","#..#.","#####","...#.","...#."],"5":["#####","#....","####.","....#","....#","#...#",".###."],"6":[".###.","#....","#....","####.","#...#","#...#",".###."],"7":["#####","....#","...#.","..#..",".#...",".#...",".#..."],"8":[".###.","#...#","#...#",".###.","#...#","#...#",".###."],"9":[".###.","#...#","#...#",".####","....#","....#",".###."]," ":[".....",".....",".....",".....",".....",".....","....."],"·":[".....",".....",".....","..#..",".....",".....","....."],":":[".....","..#..",".....",".....",".....","..#..","....."],"/":["....#","....#","...#.","..#..",".#...","#....","#...."],"-":[".....",".....",".....",".###.",".....",".....","....."],"&":[".##..","#..#.",".##..",".#...","#.#.#","#..#.",".##.#"]};
function pixText(text, color, h) {   /* a pixel-font label as a picture, h points tall */
  const T = String(text).toUpperCase().split('').filter((c) => PFONT[c]), s = 3, ctx = new DrawContext();
  ctx.size = new Size(Math.max(1, T.length * 6 - 1) * s, 7 * s); ctx.opaque = false; ctx.respectScreenScale = true; ctx.setFillColor(color);
  T.forEach((c, i) => PFONT[c].forEach((row, y) => { for (let x = 0; x < 5; x++) if (row[x] === '#') ctx.fillRect(new Rect((i * 6 + x) * s, y * s, s, s)); }));
  return { img: ctx.getImage(), w: (Math.max(1, T.length * 6 - 1) / 7) * h, h };
}
function plabel(st, text, color, h) { const p = pixText(text, color, h), i = st.addImage(p.img); i.imageSize = new Size(p.w, p.h); return i; }
function segBar(st, color, w, h) {   /* the yellow dashed strip next to SLOT SELECT */
  const ctx = new DrawContext(), s = 2; ctx.size = new Size(w * s, h * s); ctx.opaque = false; ctx.respectScreenScale = true;
  ctx.setFillColor(new Color(color.hex, 0.55)); for (let x = 0; x + 10 <= w * s; x += 14) ctx.fillRect(new Rect(x, 0, 10, h * s));
  const i = st.addImage(ctx.getImage()); i.imageSize = new Size(w, h); return i;
}
/* a save-slot frame: tinted fill, thin room-colour edge, bright corner brackets top-left and bottom-right */
function frameImg(room, w, h, strong) {
  const s = 3, W = w * s, H = h * s, ctx = new DrawContext(), col = new Color(RC[room] || room); ctx.size = new Size(W, H); ctx.opaque = false; ctx.respectScreenScale = true;
  ctx.setFillColor(mix(room, strong ? 0.3 : 0.13)); ctx.fillRect(new Rect(0, 0, W, H));
  ctx.setStrokeColor(new Color(col.hex, strong ? 0.75 : 0.4)); ctx.setLineWidth(s); ctx.strokeRect(new Rect(s / 2, s / 2, W - s, H - s));
  ctx.setFillColor(col); const L = 9 * s, T = 2 * s;
  ctx.fillRect(new Rect(0, 0, L, T)); ctx.fillRect(new Rect(0, 0, T, L)); ctx.fillRect(new Rect(W - L, H - T, L, T)); ctx.fillRect(new Rect(W - T, H - L, T, L));
  return ctx.getImage();
}
const RICON = { Vale: 'cat', Kaeden: 'leaf', Scarlett: 'saturn', Kai: 'mount', Bella: 'book', Atlas: 'boat', Max: 'car' };
const PIXC = {};
function pixImg(name, color) {
  const k = name + (color ? color.hex : ''), P = PIX[name]; if (PIXC[k]) return PIXC[k]; if (!P) return null;
  const s = 4, ctx = new DrawContext(); ctx.size = new Size(P[0].length * s, P.length * s); ctx.opaque = false; ctx.respectScreenScale = true; ctx.setFillColor(color || INK);
  P.forEach((row, y) => { for (let x = 0; x < row.length; x++) if (row[x] === '#') ctx.fillRect(new Rect(x * s, y * s, s, s)); });
  return (PIXC[k] = ctx.getImage());
}
function ico(st, name, color, sz) { const img = pixImg(name, color); if (!img) return; const i = st.addImage(img); i.imageSize = new Size(sz, sz); if (lock) i.tintColor = color; return i; }
/* an icon + text on one line */
function it(parent, icon, iconColor, text, sz, color, kind) { const l = line(parent); l.spacing = Math.max(3, sz * 0.35); ico(l, icon, iconColor, sz * 0.95); txt(l, text, sz, color, kind); return l; }
function clk(ms) { const d = new Date(ms + 8 * 3600e3), h = d.getUTCHours(), m = d.getUTCMinutes(); return ((h + 11) % 12 + 1) + (m ? ':' + String(m).padStart(2, '0') : '') + (h < 12 ? 'am' : 'pm'); }
/* fonts (owner 20 Nov: "can the fonts be better"): rounded heavy for rooms and big numbers, monospaced for times and the
   ROUND2 badge (digits line up, a little arcade), rounded medium for the rest */
function fnt(sz, k) {
  return k === 'time' ? Font.semiboldMonospacedSystemFont(sz) : k === 'head' ? Font.heavyMonospacedSystemFont(sz) : k === 'room' ? Font.heavyRoundedSystemFont(sz)
    : k === 'big' ? Font.blackRoundedSystemFont(sz) : k ? Font.boldRoundedSystemFont(sz) : Font.mediumRoundedSystemFont(sz);
}
function txt(st, s, sz, color, bold) { const t = st.addText(String(s)); t.font = fnt(sz, bold); t.textColor = color || INK; t.lineLimit = 1; t.minimumScaleFactor = 0.6; return t; }
function rc(r) { return new Color(RC[r] || '#cccccc'); }
function line(parent) { const s = parent.addStack(); s.layoutHorizontally(); s.centerAlignContent(); s.spacing = 5; return s; }

async function load() {
  const fm = FileManager.local(), p = fm.joinPath(fm.documentsDirectory(), 'round2-widget.json');
  try {
    const r = new Request(API + '?k=' + encodeURIComponent(KEY)); r.timeoutInterval = 12;
    const j = await r.loadJSON();
    if (j && j.ok) { fm.writeString(p, JSON.stringify(j)); return j; }
    if (j && j.error) return { error: j.error === 'wrong key' ? 'Key changed – get the script again (control centre › Phone alerts & widgets)' : j.error };
  } catch (e) {}
  try { const j = JSON.parse(fm.readString(p)); j.offline = true; return j; } catch (e) { return { error: 'No connection' }; }
}

/* ---------- words ---------- */
function stateOf(r) {
  if (r.st === 'in') return { big: 'till ' + clk(r.till), short: 'till ' + clk(r.till), who: r.n, c: OK };
  if (r.st === 'clean') return { big: 'cleaning till ' + clk(r.till), short: 'cleaning', who: '', c: ACC };
  if (r.st === 'next') return { big: 'next ' + clk(r.next), short: 'next ' + clk(r.next), who: r.nn, c: MUTE };
  if (r.st === 'closed') return { big: 'closed', short: 'closed', who: '', c: MUTE };
  return { big: 'free', short: 'free', who: '', c: MUTE };
}
function nextLine(r) {   /* after the current guests: who comes next, and how long the room has in between */
  if (!r.next || r.st === 'next') return '';
  return 'then ' + clk(r.next) + (names && r.nn ? ' ' + r.nn : '') + (r.gap != null && r.gap <= 30 ? ' · ' + r.gap + ' min turnover' : '');
}
function tvOf(r) { const t = r.tv; return !t ? '' : t.off ? 'TV off' : t.quiet ? 'TV not reporting' : t.app || 'TV on'; }
function ps4Of(r) { const p = r.ps4; return !p ? '' : !p.on ? 'PS4 asleep' : p.g || 'PS4 menu'; }
function msgName(m) { return m.k === 'wrapup' ? '10-min warning' : m.k === 'timeup' ? 'That’s a wrap' : m.ti || 'TV message'; }
function allMsgs(j) { const L = []; for (const r of j.rooms || []) for (const m of r.msgs || []) L.push(Object.assign({ r: r.r }, m)); return L.sort((a, b) => a.at - b.at); }
/* when to look again: the next change (an end, a start, cleaning done, a TV message), at least 2 min, at most 15 */
function refreshAt(j) {
  let t = Date.now() + 15 * 60e3;
  for (const r of j.rooms || []) for (const x of [r.till, r.next].concat((r.msgs || []).map((m) => m.at))) if (x && x > Date.now() && x < t) t = x + 30e3;
  return new Date(Math.max(t, Date.now() + 2 * 60e3));
}

/* ---------- pieces ---------- */
function header(w, j, sz, label) {
  const h = line(w); h.spacing = 5;
  plabel(h, 'ROUND2', ACC, sz * 0.75);
  const c = j.counts || {}; plabel(h, label || (c.in + ' IN · ' + c.left + ' TO COME'), INK, sz * 0.62);
  segBar(h, ACC, fam === 'small' ? 14 : 40, sz * 0.55); h.addSpacer();
  txt(h, (j.offline ? 'offline · ' : '') + clk(j.now), sz - 2, j.offline ? WARN : MUTE, 'time');
}
function warnLine(w, j, sz, n) {
  const L = j.warn || [];
  if (!L.length) { it(w, 'check', OK, 'All TVs reporting', sz, OK); return; }
  for (const x of L.slice(0, n || 1)) it(w, 'warn', WARN, x, sz, WARN, true);
}
/* ---------- colour boxes (owner 20 Nov: "box them into their colours – this layout isn't efficient") ----------
   Two columns of boxes tinted in each room's colour. Box widths come from the screen size (widgets don't say their own size). */
function mix(hex, a) {   /* the room colour at a% over the dark background */
  const h = (RC[hex] || hex).replace('#', ''), bg = [11, 9, 20], c = [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16));
  return new Color('#' + c.map((v, i) => Math.round(bg[i] + (v - bg[i]) * a).toString(16).padStart(2, '0')).join(''));
}
function widgetH() { const sw = Device.screenSize().width; return sw <= 360 ? 311 : sw <= 375 ? 345 : sw <= 393 ? 354 : sw <= 402 ? 366 : 382; }
function widgetW() { const sw = Device.screenSize().width; return sw <= 360 ? 292 : sw <= 375 ? 329 : sw <= 393 ? 338 : sw <= 402 ? 345 : 364; }
function tvTone(r) { return r.tv && (r.tv.quiet || (r.tv.off && r.st === 'in')) ? WARN : INK; }
function box(parent, r, w, h, wide) {
  const s = stateOf(r), b = parent.addStack(); b.layoutVertically(); b.size = new Size(w, h); b.setPadding(5, 7, 5, 6); b.spacing = 1;
  b.backgroundImage = frameImg(r.r, w, h, r.st === 'in');
  const top = line(b); top.spacing = 4; ico(top, RICON[r.r], rc(r.r), wide ? 13 : 11); txt(top, r.r, wide ? 13.5 : 11.5, rc(r.r), 'room'); top.addSpacer(); txt(top, s.short.replace(/^till /, '→ '), wide ? 11.5 : 10, r.st === 'in' ? INK : MUTE, 'time');
  const tv = tvOf(r), ps = r.ps4 && r.ps4.on ? ps4Of(r) : '';
  if (!wide) {   /* medium: one more line – the game if the PS4 is on, else the TV, else the guest */
    if (ps) it(b, 'pad', INK, (names && s.who ? s.who + ' · ' : '') + ps, 9.5, INK);
    else if (tv && r.st === 'in') it(b, 'tv', tvTone(r), (names && s.who ? s.who + ' · ' : '') + tv, 9.5, tvTone(r));
    else if (names && s.who) it(b, 'user', MUTE, s.who, 9.5, INK);
    b.addSpacer(); return;
  }
  if (names && s.who) it(b, 'user', MUTE, s.who, 10.5, INK, true);
  if (tv) it(b, 'tv', tvTone(r) === WARN ? WARN : MUTE, tv, 10, tvTone(r));
  if (ps) it(b, 'pad', MUTE, ps, 10, INK);
  const m = (r.msgs || [])[0]; if (m) it(b, /birthday|anniv|congrat/i.test(m.ti || '') ? 'cake' : 'bell', MUTE, clk(m.at) + ' ' + msgName(m), 9.5, MUTE);
  const nl = nextLine(r); if (nl) it(b, 'clock', MUTE, nl.replace(/^then /, 'next '), 9.5, MUTE);
  b.addSpacer();
}
/* how many detail lines a big box will show – rows of busy rooms get the room, rows of two free rooms stay short */
function boxLines(r) { const s = stateOf(r), ps = r.ps4 && r.ps4.on ? ps4Of(r) : ''; return (names && s.who ? 1 : 0) + (tvOf(r) ? 1 : 0) + (ps ? 1 : 0) + ((r.msgs || []).length ? 1 : 0) + (nextLine(r) ? 1 : 0); }
function boxGrid(w, L, h, wide) {
  const gap = 6, bw = Math.floor((widgetW() - 20 - gap) / 2), rows = [];
  for (let i = 0; i < L.length; i += 2) rows.push(L.slice(i, i + 2));
  let hs = rows.map(() => h);
  if (wide) {   /* h = the space for all rows; share it out by how much each row has to say */
    const n = rows.map((rw) => Math.max(...rw.map(boxLines))), quiet = n.filter((x) => !x).length, qh = Math.min(48, h / rows.length);
    const need = n.map((x) => 26 + 14 * x), busy = need.filter((x, i) => n[i]).reduce((a, b) => a + b, 0), k = busy ? (h - quiet * qh) / busy : 0;
    hs = n.map((x, i) => Math.floor(x ? need[i] * k : (busy ? qh : h / rows.length)));
  }
  for (let i = 0; i < L.length; i += 2) {
    const row = w.addStack(), rh = hs[i / 2]; row.layoutHorizontally(); row.spacing = gap; h = rh;
    box(row, L[i], bw, h, wide); if (L[i + 1]) box(row, L[i + 1], bw, h, wide); else { const e = row.addStack(); e.size = new Size(bw, h); }
    if (i + 2 < L.length) w.addSpacer(gap);
  }
}
/* tv row: Room · TV app (since) · PS4 game */
function tvRow(parent, r, wide) {
  const sz = wide ? 12 : 10.5, row = line(parent); row.spacing = 4;
  ico(row, RICON[r.r], rc(r.r), sz); const nm = row.addStack(); nm.size = new Size(sz * 5.2, 0); txt(nm, r.r, sz, rc(r.r), 'room');
  ico(row, 'tv', tvTone(r) === WARN ? WARN : MUTE, sz * 0.9); txt(row, (tvOf(r) || '–') + (wide && r.tv && r.tv.at && !r.tv.off ? ' · ' + clk(r.tv.at) : ''), sz, tvTone(r));
  const p = ps4Of(r); if (p) { row.addSpacer(4); ico(row, 'pad', MUTE, sz * 0.9); txt(row, p, sz, r.ps4 && r.ps4.on ? INK : MUTE); }
  row.addSpacer();
}
function timeRow(parent, at, room, what, sz) {
  const row = line(parent); row.spacing = 4;
  const tm = row.addStack(); tm.size = new Size(sz * 4.4, 0); txt(tm, clk(at), sz, INK, 'time');
  ico(row, RICON[room], rc(room), sz); txt(row, room, sz, rc(room), 'room'); txt(row, what, sz, MUTE); row.addSpacer();
}

/* ---------- widgets ---------- */
async function build() {
  const j = await load(), w = new ListWidget();
  w.url = CC + '?tab=' + (view === 'tv' ? 'tvs' : view === 'msgs' ? 'tonight' : 'insession');
  if (j.error) { if (!lock) w.backgroundColor = BG; txt(w, 'ROUND2', 13, ACC, 'head'); txt(w, j.error, 11, WARN).lineLimit = 3; return w; }
  w.refreshAfterDate = refreshAt(j);
  const c = j.counts || {}, rooms = (j.rooms || []).filter((r) => !only || r.r.toLowerCase() === only), open = rooms.filter((r) => r.st !== 'closed');
  const one = only && rooms[0], msgs = allMsgs(j).filter((m) => !only || m.r.toLowerCase() === only), ev = (j.events || []).filter((e) => !only || e.r.toLowerCase() === only);
  const evWhat = (e) => (e.t === 'in' ? '▶ in' : '■ out') + (names && e.n ? ' · ' + e.n : '');

  /* ----- lock screen (one colour, no names) ----- */
  if (fam === 'accessoryInline') {
    const e = j.nextEnd, s = j.nextStart, m = msgs[0], x = ev[0];
    w.addText(one ? one.r + ' ' + stateOf(one).short + (tvOf(one) ? ' · ' + tvOf(one) : '')
      : view === 'msgs' ? (m ? clk(m.at) + ' ' + m.r + ' ' + msgName(m) : 'No TV messages coming')
      : view === 'times' ? (x ? clk(x.at) + ' ' + x.r + ' ' + (x.t === 'in' ? 'in' : 'out') : 'Nothing else tonight')
      : e && (!s || e.at <= s.at) ? e.r + ' out ' + clk(e.at) + (s ? ' · ' + s.r + ' in ' + clk(s.at) : '')
      : s ? s.r + ' in ' + clk(s.at) + ' · ' + c.in + ' in now' : c.in + ' in · nothing else tonight');
    return w;
  }
  if (fam === 'accessoryCircular') {
    w.addAccessoryWidgetBackground = true;
    const st = w.addStack(); st.layoutVertically(); st.centerAlignContent();
    const a = line(st); a.addSpacer(); txt(a, one ? one.r.slice(0, 4) : c.in + '/' + c.open, 18, INK, true); a.addSpacer();
    const b = line(st); b.addSpacer(); txt(b, one ? (one.st === 'in' ? clk(one.till) : one.st) : (j.warn || []).length ? '⚠︎ ' + j.warn.length : 'IN', 10, MUTE); b.addSpacer();
    return w;
  }
  if (fam === 'accessoryRectangular') {
    if (one) {
      const s = stateOf(one), m = (one.msgs || [])[0];
      txt(w, one.r + ' ' + s.short, 13, INK, true);
      txt(w, [tvOf(one), one.ps4 && one.ps4.on ? ps4Of(one) : ''].filter(Boolean).join(' · ') || nextLine(one) || '–', 12, INK);
      txt(w, m ? '🔔 ' + clk(m.at) + ' ' + msgName(m) : nextLine(one) || (one.next && one.st !== 'next' ? 'next ' + clk(one.next) : ''), 11, MUTE);
      return w;
    }
    if (view === 'msgs') { txt(w, 'TV messages', 12, INK, true); for (const m of msgs.slice(0, 2)) txt(w, clk(m.at) + ' ' + m.r + ' · ' + msgName(m), 12, INK); if (!msgs.length) txt(w, 'None coming up', 12, MUTE); return w; }
    if (view === 'times') { for (const x of ev.slice(0, 3)) txt(w, clk(x.at) + ' ' + x.r + ' ' + (x.t === 'in' ? 'in' : 'out'), 12, INK); if (!ev.length) txt(w, 'Nothing else tonight', 12, MUTE); return w; }
    if (view === 'tv') { for (const r of open.filter((r) => r.st === 'in').slice(0, 3)) txt(w, r.r + ' · ' + (tvOf(r) || '–') + (r.ps4 && r.ps4.on ? ' · ' + ps4Of(r) : ''), 12, INK); if (!c.in) txt(w, 'No rooms in use', 12, MUTE); return w; }
    txt(w, 'R2 · ' + c.in + ' in · ' + c.left + ' to come', 13, INK, true);
    const e = j.nextEnd, s = j.nextStart;
    txt(w, e ? 'Out ' + clk(e.at) + ' · ' + e.r : 'Nobody in now', 12, INK);
    txt(w, (j.warn || []).length ? '⚠︎ ' + j.warn[0] : s ? 'In ' + clk(s.at) + ' · ' + s.r : 'No more sessions tonight', 12, INK);
    return w;
  }

  /* ----- home screen ----- */
  w.backgroundColor = BG; w.setPadding(12, 12, 12, 12);
  { const g = new LinearGradient(); g.colors = [new Color('#15111f'), new Color('#0b0914')]; g.locations = [0, 1]; w.backgroundGradient = g; }
  const n = fam === 'small' ? 4 : fam === 'medium' ? 5 : 13;

  if (one) {   /* one room in detail, in its colour */
    w.backgroundColor = mix(one.r, one.st === 'in' ? 0.3 : 0.14);
    const s = stateOf(one), h = line(w); ico(h, RICON[one.r], rc(one.r), big ? 20 : 16); txt(h, one.r, big ? 20 : 16, rc(one.r), 'room'); h.addSpacer(); txt(h, clk(j.now), 10, MUTE, 'time');
    w.addSpacer(4); txt(w, s.big, big ? 16 : 13.5, s.c === MUTE ? INK : s.c, 'time'); if (names && s.who) it(w, 'user', MUTE, s.who, 11, INK, true);
    if (fam !== 'small' || one.st === 'in') { if (tvOf(one)) it(w, 'tv', MUTE, tvOf(one) + (one.tv && one.tv.at && !one.tv.off && fam !== 'small' ? ' · since ' + clk(one.tv.at) : ''), 11, tvTone(one)); if (ps4Of(one)) it(w, 'pad', MUTE, ps4Of(one), 11, INK); }
    w.addSpacer();
    for (const m of (one.msgs || []).slice(0, fam === 'small' ? 1 : 3)) it(w, /birthday|anniv|congrat/i.test(m.ti || '') ? 'cake' : 'bell', MUTE, clk(m.at) + ' ' + msgName(m), 10.5, MUTE);
    const nl = nextLine(one); if (nl) it(w, 'clock', MUTE, nl.replace(/^then /, 'next '), 10.5, MUTE);
    return w;
  }

  if (view === 'msgs') {   /* TV messages coming up, every room, in time order */
    header(w, j, fam === 'small' ? 10 : 12, fam === 'small' ? 'MSGS' : 'TV MESSAGES'); w.addSpacer(6);
    for (const m of msgs.slice(0, n)) { timeRow(w, m.at, m.r, msgName(m), fam === 'small' ? 10 : 11.5); w.addSpacer(3); }
    if (!msgs.length) txt(w, 'Nothing scheduled', 11, MUTE);
    w.addSpacer(); return w;
  }
  if (view === 'times') {   /* tonight's starts and ends still to come */
    header(w, j, fam === 'small' ? 10 : 12, 'TONIGHT'); w.addSpacer(6);
    for (const x of ev.slice(0, n)) { timeRow(w, x.at, x.r, evWhat(x), fam === 'small' ? 10 : 11.5); w.addSpacer(3); }
    if (!ev.length) txt(w, 'Nothing else tonight', 11, MUTE);
    w.addSpacer(); return w;
  }
  if (view === 'tv') {   /* what's on each TV and PS4 */
    header(w, j, fam === 'small' ? 10 : 12, 'TV & PS4'); w.addSpacer(6);
    for (const r of open) { tvRow(w, r, big); w.addSpacer(big ? 6 : 2); }
    w.addSpacer(); if (fam !== 'small') warnLine(w, j, 10); return w;
  }

  /* board (default) */
  if (fam === 'small') {   /* how busy, and the next two things that happen */
    const hd = line(w); hd.spacing = 4; plabel(hd, 'ROUND2', ACC, 8); segBar(hd, ACC, 40, 6); w.addSpacer(4);
    const bg = line(w); bg.bottomAlignContent(); txt(bg, String(c.in), 36, INK, 'big'); txt(bg, ' / ' + c.open + ' in', 13, MUTE, true);
    plabel(w, c.done + ' DONE · ' + c.left + ' TO COME', MUTE, 6); w.addSpacer();
    const e = j.nextEnd, s = j.nextStart;
    for (const [lab, x] of [['out', e], ['in', s]]) if (x) { const l = line(w); l.spacing = 3; ico(l, RICON[x.r], rc(x.r), 11); txt(l, x.r + ' ' + lab, 11, rc(x.r), 'room'); const d = l.addDate(new Date(x.at)); d.applyRelativeStyle(); d.font = fnt(11, 'time'); d.textColor = INK; }
    if ((j.warn || []).length) it(w, 'warn', WARN, j.warn[0], 10, WARN);
    return w;
  }
  w.setPadding(10, 10, 10, 10);
  if (fam === 'medium') {   /* every room as a small colour box: name, status, TV / PS4 */
    header(w, j, 11); w.addSpacer(5);
    boxGrid(w, open, open.length > 4 ? 36 : 54, false);
    w.addSpacer(); if ((j.warn || []).length) warnLine(w, j, 9.5);
    return w;
  }
  header(w, j, 12); w.addSpacer(6);
  /* large: the rows share what's left after the header and the warning line */
  const warnH = (j.warn || []).length ? 30 : 0; boxGrid(w, open, widgetH() - 20 - 24 - warnH - 6 * (Math.ceil(open.length / 2) - 1), true);
  w.addSpacer(); warnLine(w, j, 10.5, 2);
  return w;
}

const widget = await build();
if (config.runsInWidget) Script.setWidget(widget);
else await (fam === 'small' ? widget.presentSmall() : fam === 'medium' ? widget.presentMedium() : widget.presentLarge());
Script.complete();
