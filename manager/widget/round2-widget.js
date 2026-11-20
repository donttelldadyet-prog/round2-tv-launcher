// Round2 – iPhone widgets (Scriptable app). Made by the control centre › Tests & systems › Phone alerts & widgets (20 Nov 2026).
// Home screen: hold an empty spot → Edit → Add widget → Scriptable → pick a size → tap the new widget → Script: Round2.
// Lock screen: hold the lock screen → Customize → Lock Screen → tap the widget row → Scriptable → tap it → Script: Round2.
// "Parameter" on a widget (optional, words can be combined, e.g. "tv nonames"):
//   (empty)   rooms board: who's in till when, what's on the TV and PS4, next TV message
//   tv        what each room's TV is showing and which PS4 game is running
//   msgs      TV messages coming up (10-minute warnings, wrap-ups, guest messages), in time order
//   times     tonight's start and end times, in time order
//   money     today's bookings: how many were made today, paid / not paid, cancelled, the latest one
//   health    the health light in full (SimplyBook use, background timers, payments to sort, TVs not reporting)
//   tomorrow  tomorrow's first session and rooms (the board shows this by itself once tonight's last guest has left)
//   count     lock screen: the next room to finish, counting down live (the round lock widget does this without a word)
//   room:Kai  just one room, in detail          nonames   hide guest names
// Data refreshes every few minutes (iOS decides exactly when). Tap a widget to open the control centre.
const KEY = '__KEY__', API = '__URL__', CC = 'https://staff.round2.sg/';
const RC = { Vale: '#E8436A', Kaeden: '#4DB848', Scarlett: '#F2C21B', Bella: '#7A5BE0', Kai: '#24B49A', Atlas: '#E9E7EF', Max: '#E0402F' };
const BG = new Color('#0b0914'), INK = new Color('#f4f1fb'), MUTE = new Color('#8d84a6'), ACC = new Color('#F2C21B'), WARN = new Color('#ff9f43'), OK = new Color('#4DB848'), BAD = new Color('#ff4d5e');
const fam = config.widgetFamily || 'large';
const par = String(args.widgetParameter || '').toLowerCase();
const lock = /^accessory/.test(fam), names = !/nonames/.test(par) && !lock;
const only = (par.match(/room:\s*([a-z]+)/) || [])[1];
const view = /\btv\b/.test(par) ? 'tv' : /\bmsgs?\b|notif/.test(par) ? 'msgs' : /\btimes?\b/.test(par) ? 'times' : /\bmoney|sales|\$/.test(par) ? 'money'
  : /\bhealth|status\b/.test(par) ? 'health' : /\btomorrow|tmr\b/.test(par) ? 'tomorrow' : /\bcount|timer\b/.test(par) ? 'count' : /\brooms\b/.test(par) ? 'rooms' : 'board';
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
/* the Round2 logo – [ROUND ▷ II] in its brackets (owner 20 Nov: "ROUND2 in yellow should be our actual logo"), from the
   website's round2-logo.png, already yellow; 298×76 */
const LOGO = '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';
let LOGO_IMG = null;
function logo(st, h, color) {
  try { LOGO_IMG = LOGO_IMG || Image.fromData(Data.fromBase64String(LOGO)); } catch (e) { return plabel(st, 'ROUND2', ACC, h * 0.8); }
  const i = st.addImage(LOGO_IMG); i.imageSize = new Size(h * 298 / 76, h); if (color) i.tintColor = color; return i;
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

function sgd(v) { return 'S$' + (Math.round(+v * 100) / 100).toFixed(2).replace(/\.00$/, ''); }
function dayName(ms) { const d = new Date(ms + 8 * 3600e3); return ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getUTCDay()] + ' ' + d.getUTCDate(); }
function ago(t) { const m = Math.round((Date.now() - t) / 60e3); return m < 1 ? 'just now' : m < 60 ? m + ' min ago' : Math.round(m / 60) + 'h ago'; }
/* the health light (owner 20 Nov): green all fine · orange keep an eye · red needs someone now */
function hl(j) { return j.health || { lv: (j.warn || []).length ? 1 : 0, list: (j.warn || []).map((t) => ({ lv: 1, t })) }; }
function hcol(lv) { return lv >= 2 ? BAD : lv ? WARN : OK; }
function dot(st, color, sz) {
  const ctx = new DrawContext(), s = 3; ctx.size = new Size(sz * s, sz * s); ctx.opaque = false; ctx.respectScreenScale = true;
  ctx.setFillColor(new Color(color.hex, 0.3)); ctx.fillEllipse(new Rect(0, 0, sz * s, sz * s)); ctx.setFillColor(color); ctx.fillEllipse(new Rect(sz * 0.75, sz * 0.75, sz * 1.5, sz * 1.5));
  const i = st.addImage(ctx.getImage()); i.imageSize = new Size(sz, sz); return i;
}
/* tonight is over: nobody in, nobody cleaning, nothing more before 7am (late-night bookings count as tonight) */
function over(j) { return !!(j.tomorrow && j.tomorrow.over); }

/* ---------- pieces ---------- */
function header(w, j, sz, label) {
  const h = line(w); h.spacing = 5;
  logo(h, sz * 1.3);
  const c = j.counts || {}; plabel(h, label || (c.in + ' IN · ' + c.left + ' TO COME'), INK, sz * 0.62);
  segBar(h, ACC, fam === 'small' ? 14 : 40, sz * 0.55); h.addSpacer();
  dot(h, hcol(hl(j).lv), sz * 0.7);
  txt(h, (j.offline ? 'offline · ' : '') + clk(j.now), sz - 2, j.offline ? WARN : MUTE, 'time');
}
function warnLine(w, j, sz, n) {   /* the health light's top lines (or "all fine") */
  const H = hl(j);
  if (!H.list.length) { it(w, 'check', OK, j.health ? 'All systems OK' : 'All TVs reporting', sz, OK); return; }
  for (const x of H.list.slice(0, n || 1)) it(w, 'warn', hcol(x.lv), x.t.replace(/^[^\w"]+/, ''), sz, hcol(x.lv), true);
}
/* (3) money today: one line for the board, a block for the money widget */
function moneyLine(w, j, sz) {
  const M = j.money; if (!M) return;
  const l = line(w); l.spacing = 4; ico(l, 'sparkle', ACC, sz);
  txt(l, M.n + ' new today', sz, INK, true); if (M.paid) txt(l, '· ' + sgd(M.paid) + ' paid', sz, OK, true);
  if (M.nUnpaid) txt(l, '· ' + M.nUnpaid + ' unpaid', sz, WARN, true); l.addSpacer();
}
/* (4) tomorrow: first session, how many, which rooms */
function tomorrowBlock(w, j, sz) {
  const T = j.tomorrow; if (!T) return;
  if (!T.n) { it(w, 'moon', MUTE, 'Nothing booked tomorrow yet', sz, MUTE); return; }
  const f = T.first;
  if (fam === 'small') {   /* first in, big; then the day, count and room icons */
    txt(w, clk(f.at), 22, INK, 'time'); const a = line(w); a.spacing = 4; ico(a, RICON[f.r], rc(f.r), 13); txt(a, f.r + ' first', 12, rc(f.r), 'room'); a.addSpacer();
    w.addSpacer(3); const b = line(w); b.spacing = 3; txt(b, dayName(f.at) + ' · ' + T.n, 10.5, INK, true); for (const r of (T.rooms || []).slice(0, 4)) ico(b, RICON[r], rc(r), 10); b.addSpacer(); return;
  }
  if (big && T.list && T.list.length > 1) {   /* large: tomorrow's sessions in time order */
    const b = line(w); b.spacing = 3; txt(b, dayName(f.at) + ' · ' + T.n + ' session' + (T.n === 1 ? '' : 's'), sz, INK, true); b.addSpacer(); w.addSpacer(4);
    for (const x of T.list.slice(0, 8)) { timeRow(w, x.at, x.r, 'till ' + clk(x.e) + (names && x.n ? ' · ' + x.n : ''), sz); w.addSpacer(3); }
    return;
  }
  const a = line(w); a.spacing = 5;
  txt(a, clk(f.at), sz * 1.5, INK, 'time'); ico(a, RICON[f.r], rc(f.r), sz * 1.3); txt(a, f.r, sz * 1.3, rc(f.r), 'room'); if (names && f.n) txt(a, f.n, sz, MUTE); a.addSpacer();
  w.addSpacer(3);
  const b = line(w); b.spacing = 3; txt(b, dayName(f.at) + ' · ' + T.n + ' session' + (T.n === 1 ? '' : 's'), sz, INK, true); b.addSpacer(4);
  for (const r of T.rooms || []) ico(b, RICON[r], rc(r), sz);
  b.addSpacer();
  if (T.last && T.n > 1) it(w, 'clock', MUTE, 'last out ' + clk(T.last), sz * 0.9, MUTE);
}
/* ---------- colour boxes (owner 20 Nov: "box them into their colours – this layout isn't efficient") ----------
   Two columns of boxes tinted in each room's colour. Box widths come from the screen size (widgets don't say their own size). */
function mix(hex, a) {   /* the room colour at a% over the dark background */
  const h = (RC[hex] || hex).replace('#', ''), bg = [11, 9, 20], c = [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16));
  return new Color('#' + c.map((v, i) => Math.round(bg[i] + (v - bg[i]) * a).toString(16).padStart(2, '0')).join(''));
}
function widgetH() { const sw = Device.screenSize().width; return sw <= 360 ? 311 : sw <= 375 ? 345 : sw <= 393 ? 354 : sw <= 402 ? 366 : 382; }
function widgetW() { const sw = Device.screenSize().width; return sw <= 360 ? 292 : sw <= 375 ? 329 : sw <= 393 ? 338 : sw <= 402 ? 345 : 364; }
/* how long this guest has left (owner 20 Nov: "how many mins to go for this guest") – a live countdown iPhone ticks itself,
   so it stays right between widget refreshes. Orange in the last 15 minutes. */
function leftLine(parent, r, sz, extra) {
  const l = line(parent); l.spacing = 3; const soon = r.till - Date.now() < 15 * 60e3;
  ico(l, 'clock', soon ? WARN : OK, sz * 0.95);
  const d = l.addDate(new Date(r.till)); d.applyTimerStyle(); d.font = fnt(sz, 'time'); d.textColor = soon ? WARN : OK; d.lineLimit = 1;
  txt(l, extra ? 'left · ' + extra : 'left', sz, extra ? INK : MUTE, extra ? '' : undefined); l.addSpacer(); return l;
}
function tvTone(r) { return r.tv && (r.tv.quiet || (r.tv.off && r.st === 'in')) ? WARN : INK; }
function box(parent, r, w, h, wide) {
  const s = stateOf(r), b = parent.addStack(); b.layoutVertically(); b.size = new Size(w, h); b.setPadding(5, 7, 5, 6); b.spacing = 1;
  b.backgroundImage = frameImg(r.r, w, h, r.st === 'in');
  const top = line(b); top.spacing = 4; ico(top, RICON[r.r], rc(r.r), wide ? 13 : 11); txt(top, r.r, wide ? 13.5 : 11.5, rc(r.r), 'room'); top.addSpacer();
  if (!wide && r.st === 'in' && r.till) {   /* medium: end time + the live minutes to go on the name line */
    txt(top, clk(r.till), 9.5, MUTE, 'time'); const soon = r.till - Date.now() < 15 * 60e3, d = top.addDate(new Date(r.till)); d.applyTimerStyle(); d.font = fnt(10.5, 'time'); d.textColor = soon ? WARN : OK; d.lineLimit = 1;
  } else txt(top, s.short.replace(/^till /, '→ '), wide ? 11.5 : 10, r.st === 'in' ? INK : MUTE, 'time');
  const tv = tvOf(r), ps = r.ps4 && r.ps4.on ? ps4Of(r) : '';
  if (!wide) {   /* medium: one more line – the game if the PS4 is on, else the TV, else the guest */
    if (ps) it(b, 'pad', INK, (names && s.who ? s.who + ' · ' : '') + ps, 9.5, INK);
    else if (tv && r.st === 'in') it(b, 'tv', tvTone(r), (names && s.who ? s.who + ' · ' : '') + tv, 9.5, tvTone(r));
    else if (names && s.who) it(b, 'user', MUTE, s.who, 9.5, INK);
    b.addSpacer(); return;
  }
  if (names && s.who) it(b, 'user', MUTE, s.who, 10.5, INK, true);
  if (r.st === 'in' && r.till) leftLine(b, r, 10.5);
  if (tv) it(b, 'tv', tvTone(r) === WARN ? WARN : MUTE, tv, 10, tvTone(r));
  if (ps) it(b, 'pad', MUTE, ps, 10, INK);
  const m = (r.msgs || [])[0]; if (m) it(b, /birthday|anniv|congrat/i.test(m.ti || '') ? 'cake' : 'bell', MUTE, clk(m.at) + ' ' + msgName(m), 9.5, MUTE);
  const nl = nextLine(r); if (nl) it(b, 'clock', MUTE, nl.replace(/^then /, 'next '), 9.5, MUTE);
  b.addSpacer();
}
/* how many detail lines a big box will show – rows of busy rooms get the room, rows of two free rooms stay short */
function boxLines(r) { const s = stateOf(r), ps = r.ps4 && r.ps4.on ? ps4Of(r) : ''; return (r.st === 'in' && r.till ? 1 : 0) + (names && s.who ? 1 : 0) + (tvOf(r) ? 1 : 0) + (ps ? 1 : 0) + ((r.msgs || []).length ? 1 : 0) + (nextLine(r) ? 1 : 0); }
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
  if (j.error) { if (!lock) w.backgroundColor = BG; { const l = line(w); logo(l, 14, lock ? INK : null); l.addSpacer(); } txt(w, j.error, 11, WARN).lineLimit = 3; return w; }
  w.refreshAfterDate = refreshAt(j);
  const c = j.counts || {}, rooms = (j.rooms || []).filter((r) => !only || r.r.toLowerCase() === only), open = rooms.filter((r) => r.st !== 'closed');
  const one = only && rooms[0], msgs = allMsgs(j).filter((m) => !only || m.r.toLowerCase() === only), ev = (j.events || []).filter((e) => !only || e.r.toLowerCase() === only);
  const evWhat = (e) => (e.t === 'in' ? '▶ in' : '■ out') + (names && e.n ? ' · ' + e.n : '');

  /* ----- lock screen (one colour, no names) ----- */
  /* (6) countdown (owner 20 Nov): the next room to finish, live – iOS counts the seconds itself, no refresh needed */
  const cd = one ? (one.st === 'in' ? { r: one.r, at: one.till, t: 'out' } : one.next ? { r: one.r, at: one.next, t: 'in' } : null)
    : j.nextEnd ? { r: j.nextEnd.r, at: j.nextEnd.at, t: 'out' } : j.nextStart ? { r: j.nextStart.r, at: j.nextStart.at, t: 'in' } : null;
  const timer = (st, at, sz, color) => { const d = st.addDate(new Date(at)); d.applyTimerStyle(); d.font = fnt(sz, 'time'); d.textColor = color || INK; d.lineLimit = 1; d.minimumScaleFactor = 0.5; return d; };
  if (fam === 'accessoryCircular' && view !== 'rooms') {
    w.addAccessoryWidgetBackground = true;
    const st = w.addStack(); st.layoutVertically(); st.centerAlignContent();
    if (!cd) { const a = line(st); a.addSpacer(); if (one) ico(a, RICON[one.r], rc(one.r), 18); else txt(a, c.in + '/' + c.open, 18, INK, true); a.addSpacer(); const b = line(st); b.addSpacer(); txt(b, one ? one.st.toUpperCase() : over(j) ? 'DONE' : 'IN', 10, MUTE); b.addSpacer(); return w; }
    const a = line(st); a.addSpacer(); ico(a, RICON[cd.r], rc(cd.r), 15); a.addSpacer();
    const b = line(st); b.addSpacer(); timer(b, cd.at, 13); b.addSpacer();
    const e = line(st); e.addSpacer(); txt(e, cd.t === 'out' ? 'OUT' : 'IN', 8, MUTE, true); e.addSpacer();
    return w;
  }
  if (fam === 'accessoryRectangular' && view === 'count') {
    if (!cd) { txt(w, over(j) ? 'Tonight done' : 'Nobody in', 13, INK, true); const T = j.tomorrow; if (T && T.first) txt(w, 'Tomorrow ' + clk(T.first.at) + ' ' + T.first.r, 12, INK); return w; }
    const a = line(w); a.spacing = 4; ico(a, RICON[cd.r], rc(cd.r), 14); txt(a, cd.r + (cd.t === 'out' ? ' out ' : ' in ') + clk(cd.at), 13, INK, 'room'); a.addSpacer();
    timer(w, cd.at, 22);
    const nx = cd.t === 'out' && j.nextStart ? j.nextStart : null; if (nx) txt(w, 'then ' + nx.r + ' in ' + clk(nx.at), 11, MUTE);
    return w;
  }
  if (fam === 'accessoryRectangular' && view === 'money') {
    const M = j.money; if (!M) { txt(w, 'Money: update the engine', 12, INK); return w; }
    txt(w, M.n + ' booked today', 14, INK, true); txt(w, sgd(M.paid) + ' paid' + (M.nUnpaid ? ' · ' + M.nUnpaid + ' not paid' : ''), 12, INK);
    txt(w, M.last ? 'Last: ' + M.last.r + ' ' + ago(M.last.t) : M.cx ? M.cx + ' cancelled' : 'No new bookings yet', 11, MUTE); return w;
  }
  if (fam === 'accessoryRectangular' && view === 'health') {
    const H = hl(j); txt(w, H.lv >= 2 ? '● Needs someone' : H.lv ? '● Keep an eye' : '● All systems OK', 13, INK, true);
    for (const x of H.list.slice(0, 2)) txt(w, x.t.replace(/^[^\w"]+/, ''), 11, INK); if (!H.list.length && j.health && j.health.sb != null) txt(w, j.health.sb + ' SimplyBook requests today', 11, MUTE); return w;
  }
  if (fam === 'accessoryInline' && view === 'count' && cd) { w.addText(cd.r + ' ' + cd.t + ' ' + clk(cd.at)); return w; }
  if (fam === 'accessoryInline') {
    const e = j.nextEnd, s = j.nextStart, m = msgs[0], x = ev[0];
    w.addText(one ? one.r + ' ' + stateOf(one).short + (tvOf(one) ? ' · ' + tvOf(one) : '')
      : view === 'money' ? (j.money ? j.money.n + ' booked today · ' + sgd(j.money.paid) + ' paid' : 'Money: update the engine')
      : view === 'health' ? (hl(j).list[0] ? '⚠︎ ' + hl(j).list[0].t.replace(/^[^\w"]+/, '') : 'Round2: all systems OK')
      : over(j) && j.tomorrow.first ? 'Tonight done · tomorrow ' + clk(j.tomorrow.first.at) + ' ' + j.tomorrow.first.r
      : view === 'msgs' ? (m ? clk(m.at) + ' ' + m.r + ' ' + msgName(m) : 'No TV messages coming')
      : view === 'times' ? (x ? clk(x.at) + ' ' + x.r + ' ' + (x.t === 'in' ? 'in' : 'out') : 'Nothing else tonight')
      : e && (!s || e.at <= s.at) ? e.r + ' out ' + clk(e.at) + (s ? ' · ' + s.r + ' in ' + clk(s.at) : '')
      : s ? s.r + ' in ' + clk(s.at) + ' · ' + c.in + ' in now' : c.in + ' in · nothing else tonight');
    return w;
  }
  if (fam === 'accessoryCircular') {   /* parameter "rooms": how many rooms are in */
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
    if (over(j)) { txt(w, 'R2 · tonight done', 13, INK, true); const T = j.tomorrow; txt(w, T.first ? 'Tomorrow ' + clk(T.first.at) + ' · ' + T.first.r : 'Nothing booked tomorrow', 12, INK); txt(w, T.n ? T.n + ' sessions tomorrow' : '', 11, MUTE); return w; }
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
    w.addSpacer(4); txt(w, s.big, big ? 16 : 13.5, s.c === MUTE ? INK : s.c, 'time'); if (one.st === 'in' && one.till) leftLine(w, one, big ? 14 : 12); if (names && s.who) it(w, 'user', MUTE, s.who, 11, INK, true);
    if (fam !== 'small' || one.st === 'in') { if (tvOf(one)) it(w, 'tv', MUTE, tvOf(one) + (one.tv && one.tv.at && !one.tv.off && fam !== 'small' ? ' · since ' + clk(one.tv.at) : ''), 11, tvTone(one)); if (ps4Of(one)) it(w, 'pad', MUTE, ps4Of(one), 11, INK); }
    w.addSpacer();
    for (const m of (one.msgs || []).slice(0, fam === 'small' ? 1 : 3)) it(w, /birthday|anniv|congrat/i.test(m.ti || '') ? 'cake' : 'bell', MUTE, clk(m.at) + ' ' + msgName(m), 10.5, MUTE);
    const nl = nextLine(one); if (nl) it(w, 'clock', MUTE, nl.replace(/^then /, 'next '), 10.5, MUTE);
    return w;
  }

  if (view === 'money') {   /* (3) today's bookings and money */
    header(w, j, fam === 'small' ? 10 : 12, 'TODAY'); w.addSpacer(fam === 'small' ? 4 : 8);
    const M = j.money; if (!M) { txt(w, 'Waiting for the engine update', 11, MUTE); return w; }
    const a = line(w); a.bottomAlignContent(); a.spacing = 4; txt(a, String(M.n), fam === 'small' ? 30 : 34, INK, 'big'); txt(a, 'booked', 12, MUTE, true);
    if (fam !== 'small') { a.addSpacer(); const p = a.addStack(); p.layoutVertically(); txt(p, sgd(M.paid), 20, OK, 'big'); txt(p, 'paid', 10, MUTE, true); }
    if (fam === 'small') it(w, 'check', OK, sgd(M.paid) + ' paid', 11, OK, true);
    if (M.nUnpaid) it(w, 'warn', WARN, M.nUnpaid + ' not paid · ' + sgd(M.unpaid), fam === 'small' ? 10.5 : 11.5, WARN, true);
    if (M.list && fam !== 'small') it(w, 'clock', MUTE, sgd(M.list) + ' booked by staff / WhatsApp (price list)', 10.5, MUTE);
    if (M.cx) it(w, 'warn', MUTE, M.cx + ' cancelled today', 10.5, MUTE);
    if (big) w.addSpacer(8); else w.addSpacer();
    if (M.last) { const x = M.last, l = line(w); l.spacing = 4; ico(l, RICON[x.r], rc(x.r), 11); txt(l, (fam === 'small' ? x.r : 'Latest: ' + x.r + (names && x.n ? ' · ' + x.n : '') + (x.s ? ' · ' + dayName(x.s) : '')), 10.5, INK, true); txt(l, ago(x.t), 10, MUTE); l.addSpacer(); }
    if (big) { w.addSpacer(12); plabel(w, 'TOMORROW', MUTE, 7); w.addSpacer(5); tomorrowBlock(w, j, 11); w.addSpacer(); plabel(w, 'HEALTH', MUTE, 7); w.addSpacer(5); warnLine(w, j, 10.5, 3); }
    return w;
  }
  if (view === 'health') {   /* (5) the health light in full */
    const H = hl(j); header(w, j, fam === 'small' ? 10 : 12, 'HEALTH'); w.addSpacer(6);
    const a = line(w); a.spacing = 6; dot(a, hcol(H.lv), fam === 'small' ? 14 : 16); txt(a, H.lv >= 2 ? 'Needs someone' : H.lv ? 'Keep an eye' : 'All OK', fam === 'small' ? 14 : 16, hcol(H.lv), 'big'); a.addSpacer();
    w.addSpacer(4);
    for (const x of H.list.slice(0, fam === 'small' ? 3 : big ? 8 : 4)) { it(w, 'warn', hcol(x.lv), x.t.replace(/^[^\w"]+/, ''), fam === 'small' ? 9.5 : 10.5, hcol(x.lv), true); w.addSpacer(2); }
    w.addSpacer();
    if (j.health && j.health.sb != null) it(w, 'check', MUTE, j.health.sb + (fam === 'small' ? ' SimplyBook calls' : ' SimplyBook requests today · limit ~10,000'), 9.5, MUTE);
    return w;
  }
  if (view === 'tomorrow' || (view === 'board' && over(j))) {   /* (4) tomorrow – by itself once tonight's last guest has left */
    header(w, j, fam === 'small' ? 10 : 12, 'TOMORROW'); w.addSpacer(fam === 'small' ? 5 : 8);
    if (!j.tomorrow) { txt(w, 'Waiting for the engine update', 11, MUTE); return w; }
    if (view === 'board') { it(w, 'moon', MUTE, fam === 'small' ? 'Tonight done' : 'Tonight done · ' + c.done + ' session' + (c.done === 1 ? '' : 's'), fam === 'small' ? 10 : 11, MUTE, true); w.addSpacer(4); }
    tomorrowBlock(w, j, fam === 'small' ? 10 : 11.5);
    w.addSpacer();
    if (fam !== 'small') moneyLine(w, j, 10.5);
    if (big) { w.addSpacer(6); warnLine(w, j, 10.5, 3); }
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
    const hd = line(w); hd.spacing = 4; logo(hd, 13); segBar(hd, ACC, 20, 6); hd.addSpacer(); dot(hd, hcol(hl(j).lv), 8); w.addSpacer(4);
    const bg = line(w); bg.bottomAlignContent(); txt(bg, String(c.in), 36, INK, 'big'); txt(bg, ' / ' + c.open + ' in', 13, MUTE, true);
    plabel(w, c.done + ' DONE · ' + c.left + ' TO COME', MUTE, 6); w.addSpacer();
    const e = j.nextEnd, s = j.nextStart;
    for (const [lab, x] of [['out', e], ['in', s]]) if (x) { const l = line(w); l.spacing = 3; ico(l, RICON[x.r], rc(x.r), 11); txt(l, x.r + ' ' + lab, 11, rc(x.r), 'room'); const d = l.addDate(new Date(x.at)); d.applyRelativeStyle(); d.font = fnt(11, 'time'); d.textColor = INK; }
    if (hl(j).list.length) { const x = hl(j).list[0]; it(w, 'warn', hcol(x.lv), x.t.replace(/^[^\w"]+/, ''), 10, hcol(x.lv)); }
    return w;
  }
  w.setPadding(10, 10, 10, 10);
  if (fam === 'medium') {   /* every room as a small colour box: name, status, TV / PS4 */
    header(w, j, 11); w.addSpacer(5);
    boxGrid(w, open, open.length > 4 ? 36 : 54, false);
    w.addSpacer(); if (hl(j).list.length) warnLine(w, j, 9.5);
    return w;
  }
  header(w, j, 12); w.addSpacer(6);
  /* large: the rows share what's left after the header and the warning line */
  const nW = Math.min(2, Math.max(1, hl(j).list.length)), footH = 15 * nW + (j.money ? 16 : 0);
  boxGrid(w, open, widgetH() - 20 - 24 - footH - 6 * (Math.ceil(open.length / 2) - 1), true);
  w.addSpacer(); if (j.money) moneyLine(w, j, 10); warnLine(w, j, 10.5, 2);
  return w;
}

const widget = await build();
if (config.runsInWidget) Script.setWidget(widget);
else await (fam === 'small' ? widget.presentSmall() : fam === 'medium' ? widget.presentMedium() : widget.presentLarge());
Script.complete();
