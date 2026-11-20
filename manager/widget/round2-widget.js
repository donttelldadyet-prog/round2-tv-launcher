// Round2 – iPhone widgets (Scriptable app). Made by the control centre › Tests & systems › Phone alerts & widgets (20 Nov 2026).
// Home screen: hold an empty spot → Edit → Add widget → Scriptable → pick a size → tap the new widget → Script: Round2.
// Lock screen: hold the lock screen → Customize → Lock Screen → tap the widget row → Scriptable → tap it → Script: Round2.
// "Parameter" on a widget (optional, words can be combined, e.g. "tv nonames"):
//   (empty)   rooms board: who's in till when, what's on the TV and PS4, next TV message
//   tv        what each room's TV is showing and which PS4 game is running
//   msgs      TV messages: the automatic 10-min warning + That's a wrap per room, then scheduled messages (birthdays, house
//             reminders, guest messages) – grey coming up · orange on the TV, waiting for its OK · green OK'd
//   times     tonight's start and end times, in time order
//   money     today's bookings: how many were made today, paid / not paid, cancelled, the latest one
//   health    the health light in full (SimplyBook use, background timers, payments to sort, TVs not reporting)
//   tomorrow  tomorrow's first session and rooms (the board shows this by itself once tonight's last guest has left)
//   count     lock screen: the next rooms to start or finish (rooms at the same time together), counting down live
//             (the round lock widget does this without a word); then the group after it
//   room:Kai  just one room, in detail          nonames   hide guest names
//   notap     tapping the widget doesn't open the control centre (iPhone then just opens Scriptable – it can't do nothing)
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
  ctx.size = new Size(Math.max(1, T.length * 6 - 1) * s, 7 * s); ctx.opaque = false; ctx.respectScreenScale = false; ctx.setFillColor(color);
  T.forEach((c, i) => PFONT[c].forEach((row, y) => { for (let x = 0; x < 5; x++) if (row[x] === '#') ctx.fillRect(new Rect((i * 6 + x) * s, y * s, s, s)); }));
  return { img: ctx.getImage(), w: (Math.max(1, T.length * 6 - 1) / 7) * h, h };
}
function plabel(st, text, color, h) { return txt(st, String(text).toUpperCase(), h * 8 / 7, color, 'pix'); }   /* now the real Press Start 2P (installed font) */
function segBar(st, color, w, h) {   /* the yellow dashed strip next to SLOT SELECT */
  const ctx = new DrawContext(), s = 3; ctx.size = new Size(w * s, h * s); ctx.opaque = false; ctx.respectScreenScale = false;
  ctx.setFillColor(new Color(color.hex, 0.55)); for (let x = 0; x + 10 <= w * s; x += 14) ctx.fillRect(new Rect(x, 0, 10, h * s));
  const i = st.addImage(ctx.getImage()); i.imageSize = new Size(w, h); return i;
}
/* a save-slot frame: tinted fill, thin room-colour edge, bright corner brackets top-left and bottom-right */
function frameImg(room, w, h, strong) {
  /* (owner's large widget came up blank white, 20 Nov 8:32am: with the screen's ×3 on top of ×3, six big frames were ~80 MB –
     iPhone stops a widget past ~30 MB. Now ×2 pixels and no screen scaling: ~4 MB in all) */
  const s = 2, W = w * s, H = h * s, ctx = new DrawContext(), col = new Color(RC[room] || room); ctx.size = new Size(W, H); ctx.opaque = false; ctx.respectScreenScale = false;
  ctx.setFillColor(mix(room, strong ? 0.3 : 0.13)); ctx.fillRect(new Rect(0, 0, W, H));
  ctx.setStrokeColor(new Color(col.hex, strong ? 0.75 : 0.4)); ctx.setLineWidth(s); ctx.strokeRect(new Rect(s / 2, s / 2, W - s, H - s));
  ctx.setFillColor(col); const L = 9 * s, T = 2 * s;
  ctx.fillRect(new Rect(0, 0, L, T)); ctx.fillRect(new Rect(0, 0, T, L)); ctx.fillRect(new Rect(W - L, H - T, L, T)); ctx.fillRect(new Rect(W - T, H - L, T, L));
  return ctx.getImage();
}
/* the Round2 logo – [ROUND ▷ II] in its brackets (owner 20 Nov: "should be our actual logo" … "logo should be white, don't
   desecrate our logo"): the website's round2-logo.png exactly as it is – white, unchanged, never recoloured; 298×76 */
const LOGO = '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';
let LOGO_IMG = null;
function logo(st, h) {
  try { LOGO_IMG = LOGO_IMG || Image.fromData(Data.fromBase64String(LOGO)); } catch (e) { return plabel(st, 'ROUND2', INK, h * 0.8); }
  const i = st.addImage(LOGO_IMG); i.imageSize = new Size(h * 298 / 76, h); return i;
}
const RICON = { Vale: 'cat', Kaeden: 'leaf', Scarlett: 'saturn', Kai: 'mount', Bella: 'book', Atlas: 'boat', Max: 'car' };
const PIXC = {};
function pixImg(name, color) {
  const k = name + (color ? color.hex : ''), P = PIX[name]; if (PIXC[k]) return PIXC[k]; if (!P) return null;
  const s = 4, ctx = new DrawContext(); ctx.size = new Size(P[0].length * s, P.length * s); ctx.opaque = false; ctx.respectScreenScale = false; ctx.setFillColor(color || INK);
  P.forEach((row, y) => { for (let x = 0; x < row.length; x++) if (row[x] === '#') ctx.fillRect(new Rect(x * s, y * s, s, s)); });
  return (PIXC[k] = ctx.getImage());
}
function ico(st, name, color, sz) { const img = pixImg(name, color); if (!img) return; const i = st.addImage(img); i.imageSize = new Size(sz, sz); if (lock) i.tintColor = color; return i; }
/* an icon + text on one line */
function it(parent, icon, iconColor, text, sz, color, kind) { const l = line(parent); l.spacing = Math.max(3, sz * 0.35); ico(l, icon, iconColor, sz * 0.95); txt(l, text, sz, color, kind); return l; }
function clk(ms) { const d = new Date(ms + 8 * 3600e3), h = d.getUTCHours(), m = d.getUTCMinutes(); return ((h + 11) % 12 + 1) + (m ? ':' + String(m).padStart(2, '0') : '') + (h < 12 ? 'am' : 'pm'); }
/* fonts (owner 20 Nov: "can the fonts be better"): rounded heavy for rooms and big numbers, monospaced for times and the
   ROUND2 badge (digits line up, a little arcade), rounded medium for the rest */
/* the website's own fonts (owner 20 Nov: "use our theme fonts"): Press Start 2P for room names, labels and big numbers,
   Chakra Petch for the rest. iPhone widgets can only use fonts installed on the phone – the control centre's "Install Round2
   fonts" button gives the profile (round2-fonts.mobileconfig). Without it iPhone quietly uses its own font. Press Start 2P
   fills its whole square, so it is set ~20% smaller to sit level with Chakra Petch. */
const PS2P = 'PressStart2P-Regular', CHK = { m: 'ChakraPetch-Medium', s: 'ChakraPetch-SemiBold', b: 'ChakraPetch-Bold' };
function fnt(sz, k) {
  return k === 'pix' ? new Font(PS2P, sz) : k === 'room' || k === 'head' ? new Font(PS2P, sz * 0.8) : k === 'big' ? new Font(PS2P, sz * 0.78)
    : k === 'time' ? new Font(CHK.s, sz * 1.08) : k ? new Font(CHK.b, sz * 1.08) : new Font(CHK.m, sz * 1.08);
}
function txt(st, s, sz, color, bold) { const t = st.addText(bold === 'room' || bold === 'head' || bold === 'big' ? String(s).toUpperCase() : String(s));   /* Press Start 2P in capitals, like the website */ t.font = fnt(sz, bold); t.textColor = color || INK; t.lineLimit = 1; t.minimumScaleFactor = 0.6; return t; }
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
/* automatic (10-min warning, That's a wrap – the engine runs them itself) vs scheduled (birthdays, house reminders, guests'
   own) – owner 20 Nov: "needs to be separate … turn a different colour when acknowledged" */
const W_ROOMS = ['Vale', 'Kaeden', 'Scarlett', 'Kai', 'Bella', 'Atlas', 'Max'];
function isAuto(m) { return m.k === 'wrapup' || m.k === 'timeup'; }
function mState(m) { return m.ok ? 'ok' : m.at > Date.now() ? 'next' : m.ak === 0 ? 'shown' : 'wait'; }
function mCol(m) { const x = mState(m); return x === 'ok' ? OK : x === 'wait' ? WARN : x === 'shown' ? MUTE : INK; }
function mIcon(m) { return isAuto(m) ? 'bell' : /birthday|bday|anniv|congrat/i.test(m.ti || '') ? 'cake' : 'sparkle'; }
function cks(ms) { const d = new Date(ms + 8 * 3600e3); return ((d.getUTCHours() + 11) % 12 + 1) + ':' + String(d.getUTCMinutes()).padStart(2, '0'); }   /* 8:13 – no am/pm, for tight spots */
function autoTag(m, short) { return (m.k === 'wrapup' ? '10m ' : 'wrap ') + (short ? cks(m.at) : clk(m.at)) + (m.ok ? ' ✓' : mState(m) === 'wait' ? ' …' : ''); }
/* one line: the room's automatic pair as coloured tags */
function autoLine(parent, L, sz, lead, short) {
  const l = line(parent); l.spacing = 6; if (lead) lead(l); else ico(l, 'bell', MUTE, sz * 0.95);
  for (const m of L.slice(0, 2)) txt(l, autoTag(m, short !== false), sz, mCol(m), 'time');
  l.addSpacer(); return l;
}
function schedLine(parent, m, sz, withRoom, long) {
  const l = line(parent); l.spacing = 4; ico(l, mIcon(m), mState(m) === 'next' ? ACC : mCol(m), sz * 0.95);
  txt(l, long ? clk(m.at) : cks(m.at), sz, mCol(m), 'time'); if (withRoom) { ico(l, RICON[m.r], rc(m.r), sz * 0.9); txt(l, m.r, sz, rc(m.r), 'room'); }
  txt(l, (m.ti || 'TV message') + (m.ok ? ' ✓' : ''), sz, mState(m) === 'next' ? INK : mCol(m)); l.addSpacer(); return l;
}
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
  const ctx = new DrawContext(), s = 3; ctx.size = new Size(sz * s, sz * s); ctx.opaque = false; ctx.respectScreenScale = false;
  ctx.setFillColor(new Color(color.hex, 0.3)); ctx.fillEllipse(new Rect(0, 0, sz * s, sz * s)); ctx.setFillColor(color); ctx.fillEllipse(new Rect(sz * 0.75, sz * 0.75, sz * 1.5, sz * 1.5));
  const i = st.addImage(ctx.getImage()); i.imageSize = new Size(sz, sz); return i;
}
/* tonight is over: nobody in, nobody cleaning, nothing more before 7am (late-night bookings count as tonight) */
function over(j) { return !!(j.tomorrow && j.tomorrow.over); }

/* ---------- pieces ---------- */
/* iPhone rounds the widget's corners (~22 pt): the header and the bottom lines sit a little further in than the boxes, so the
   logo, the time and the warnings never run into the curve (owner 20 Nov: "account for rounded edges too") */
const EDGE = 6;
function header(w, j, sz, label) {
  const h = line(w); h.spacing = 5; h.setPadding(0, fam === 'small' ? 0 : EDGE, 0, fam === 'small' ? 0 : EDGE);
  logo(h, sz * 1.3);
  const c = j.counts || {}; plabel(h, label || (c.in + ' IN · ' + c.left + ' TO COME'), INK, sz * 0.52);
  segBar(h, ACC, fam === 'small' ? 14 : 22, sz * 0.5); h.addSpacer();
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
function tvTone(r) { return r.st === 'in' && r.tv && (r.tv.quiet || r.tv.off) ? WARN : r.tv && (r.tv.off || r.tv.quiet) ? MUTE : INK; }   /* an empty room's TV being off is normal */
function box(parent, r, w, h, wide) {
  const s = stateOf(r), b = parent.addStack(); b.layoutVertically(); b.size = new Size(w, h); if (wide) b.setPadding(5, 7, 5, 6); else b.setPadding(3, 7, 3, 6); b.spacing = 1;
  b.backgroundImage = frameImg(r.r, w, h, r.st === 'in');
  const top = line(b); top.spacing = 4; ico(top, RICON[r.r], rc(r.r), wide ? 13 : 11); txt(top, r.r, wide ? 13.5 : 11.5, rc(r.r), 'room'); top.addSpacer();
  if (wide) {   /* large: the room name on its own line, then how it stands (owner 20 Nov: "line break after each room name") */
    b.addSpacer(2);
    if (r.st === 'in' && r.till) { const l = line(b); l.spacing = 4; const soon = r.till - Date.now() < 15 * 60e3; txt(l, 'till ' + clk(r.till), 11, INK, 'time');
      const d = l.addDate(new Date(r.till)); d.applyTimerStyle(); d.font = fnt(11, 'time'); d.textColor = soon ? WARN : OK; d.lineLimit = 1; txt(l, 'left', 10, MUTE); l.addSpacer(); }
    else { const l = line(b); txt(l, s.big, 11, r.st === 'clean' ? ACC : MUTE, 'time'); l.addSpacer(); }
    b.addSpacer(2);
  } else if (r.st === 'in' && r.till) {   /* medium: end time + the live minutes to go on the name line */
    txt(top, clk(r.till), 9.5, MUTE, 'time'); const soon = r.till - Date.now() < 15 * 60e3, d = top.addDate(new Date(r.till)); d.applyTimerStyle(); d.font = fnt(10.5, 'time'); d.textColor = soon ? WARN : OK; d.lineLimit = 1;
  } else txt(top, s.short, 10, MUTE, 'time');
  const tv = tvOf(r), ps = r.ps4 && r.ps4.on ? ps4Of(r) : '';
  if (!wide) {   /* medium: one more line – the game if the PS4 is on, else the TV, else the guest */
    if (ps) it(b, 'pad', INK, (names && s.who ? s.who + ' · ' : '') + ps, 9.5, INK);
    else if (tv && r.st === 'in') it(b, 'tv', tvTone(r), (names && s.who ? s.who + ' · ' : '') + tv, 9.5, tvTone(r));
    else if (names && s.who) it(b, 'user', MUTE, s.who, 9.5, INK);
    b.addSpacer(); return;
  }
  if (names && s.who) it(b, 'user', MUTE, s.who, 10.5, INK, true);
  if (tv && (r.st === 'in' || (r.tv && !r.tv.off && !r.tv.quiet))) it(b, 'tv', tvTone(r) === WARN ? WARN : MUTE, tv, 10, tvTone(r));   /* an empty room's TV: only when it's on */
  if (ps) it(b, 'pad', MUTE, ps, 10, INK);
  const A = (r.msgs || []).filter(isAuto), S = (r.msgs || []).filter((m) => !isAuto(m));
  if (A.length) autoLine(b, A, 9.5);
  if (S.length) schedLine(b, S.find((m) => mState(m) !== 'ok') || S[S.length - 1], 9.5);
  const nl = nextLine(r); if (nl) it(b, 'clock', MUTE, nl.replace(/^then /, 'next '), 9.5, MUTE);
  b.addSpacer();
}
/* how many detail lines a big box will show – rows of busy rooms get the room, rows of two free rooms stay short */
function boxLines(r) { const s = stateOf(r), ps = r.ps4 && r.ps4.on ? ps4Of(r) : ''; return 1 + (names && s.who ? 1 : 0) + (tvOf(r) && (r.st === 'in' || (r.tv && !r.tv.off && !r.tv.quiet)) ? 1 : 0) + (ps ? 1 : 0) + ((r.msgs || []).some(isAuto) ? 1 : 0) + ((r.msgs || []).some((m) => !isAuto(m)) ? 1 : 0) + (nextLine(r) ? 1 : 0); }
function boxGrid(w, L, h, wide) {
  const gap = 6, vgap = wide ? 6 : 4, bw = Math.floor((widgetW() - 20 - gap) / 2), rows = [];
  for (let i = 0; i < L.length; i += 2) rows.push(L.slice(i, i + 2));
  let hs = rows.map(() => h);
  if (wide) {   /* h = the space for all rows; share it out by how much each row has to say */
    const n = rows.map((rw) => Math.max(...rw.map(boxLines))), quiet = n.filter((x) => x <= 1).length, qh = Math.min(50, h / rows.length);
    const need = n.map((x) => 30 + 14.5 * x), busy = need.filter((x, i) => n[i] > 1).reduce((a, b) => a + b, 0), k = busy ? (h - quiet * qh) / busy : 0;
    hs = n.map((x, i) => Math.floor(x > 1 ? need[i] * k : (busy ? qh : h / rows.length)));
  }
  for (let i = 0; i < L.length; i += 2) {
    const row = w.addStack(), rh = hs[i / 2]; row.layoutHorizontally(); row.spacing = gap; h = rh;
    box(row, L[i], bw, h, wide); if (L[i + 1]) box(row, L[i + 1], bw, h, wide); else { const e = row.addStack(); e.size = new Size(bw, h); }
    if (i + 2 < L.length) w.addSpacer(vgap);
  }
}
/* tv row: Room · TV app (since) · PS4 game */
function tvRow(parent, r, wide) {
  const sz = wide ? 12 : 10.5, row = line(parent); row.spacing = 4;
  ico(row, RICON[r.r], rc(r.r), sz); const nm = row.addStack(); nm.size = new Size(sz * 6.8, 0); txt(nm, r.r, sz, rc(r.r), 'room');
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
  if (!/\bnotap\b|nolink/.test(par)) w.url = CC + '?tab=' + (view === 'tv' ? 'tvs' : view === 'msgs' ? 'tonight' : 'insession');
  if (j.error) { if (!lock) w.backgroundColor = BG; { const l = line(w); logo(l, 14); l.addSpacer(); } txt(w, j.error, 11, WARN).lineLimit = 3; return w; }
  w.refreshAfterDate = refreshAt(j);
  const c = j.counts || {}, rooms = (j.rooms || []).filter((r) => !only || r.r.toLowerCase() === only), open = rooms.filter((r) => r.st !== 'closed');
  const one = only && rooms[0], msgs = allMsgs(j).filter((m) => !only || m.r.toLowerCase() === only), ev = (j.events || []).filter((e) => !only || e.r.toLowerCase() === only);
  const evWhat = (e) => (e.t === 'in' ? '▶ in' : '■ out') + (names && e.n ? ' · ' + e.n : '');

  /* ----- lock screen (one colour, no names) ----- */
  /* (6) countdown (owner 20 Nov): the next room to finish, live – iOS counts the seconds itself, no refresh needed */
  /* rooms that start or finish at the same time count down together (owner 20 Nov: "doesn't show stacking rooms with same
     start times") – the next group of starts or ends, then the one after it */
  const G = []; for (const e of (j.events || []).filter((e) => e.at > Date.now() && (!only || e.r.toLowerCase() === only))) {
    const g = G[G.length - 1]; if (g && g.t === e.t && Math.abs(g.at - e.at) < 60e3) { if (!g.rs.includes(e.r)) g.rs.push(e.r); } else G.push({ at: e.at, t: e.t, rs: [e.r] }); }
  const cd0 = one ? (one.st === 'in' ? { r: one.r, at: one.till, t: 'out' } : one.next ? { r: one.r, at: one.next, t: 'in' } : null)
    : j.nextEnd ? { r: j.nextEnd.r, at: j.nextEnd.at, t: 'out' } : j.nextStart ? { r: j.nextStart.r, at: j.nextStart.at, t: 'in' } : null;
  const cd = !one && G[0] ? Object.assign({ r: G[0].rs[0] }, G[0]) : cd0 && Object.assign({ rs: [cd0.r] }, cd0), cd2 = (one ? G.find((g) => cd && g.at > cd.at + 60e3) : G[1]) || null;
  const grpName = (g) => g.rs.length > 2 ? g.rs.length + ' rooms' : g.rs.join(' · ');
  const timer = (st, at, sz, color) => { const d = st.addDate(new Date(at)); d.applyTimerStyle(); d.font = fnt(sz, 'time'); d.textColor = color || INK; d.lineLimit = 1; d.minimumScaleFactor = 0.5; return d; };
  if (fam === 'accessoryCircular' && view !== 'rooms') {
    w.addAccessoryWidgetBackground = true;
    const st = w.addStack(); st.layoutVertically(); st.centerAlignContent();
    if (!cd) { const a = line(st); a.addSpacer(); if (one) ico(a, RICON[one.r], rc(one.r), 18); else txt(a, c.in + '/' + c.open, 18, INK, true); a.addSpacer(); const b = line(st); b.addSpacer(); txt(b, one ? one.st.toUpperCase() : over(j) ? 'DONE' : 'IN', 10, MUTE); b.addSpacer(); return w; }
    const a = line(st); a.spacing = 1; a.addSpacer(); for (const r of cd.rs.slice(0, 3)) ico(a, RICON[r], rc(r), cd.rs.length > 1 ? 11 : 15); a.addSpacer();
    const b = line(st); b.addSpacer(); timer(b, cd.at, 13); b.addSpacer();
    const e = line(st); e.addSpacer(); txt(e, cd.t === 'out' ? 'OUT' : 'IN', 8, MUTE, true); e.addSpacer();
    return w;
  }
  if (fam === 'accessoryRectangular' && view === 'count') {
    if (!cd) { txt(w, over(j) ? 'Tonight done' : 'Nobody in', 13, INK, true); const T = j.tomorrow; if (T && T.first) txt(w, 'Tomorrow ' + clk(T.first.at) + ' ' + T.first.r, 12, INK); return w; }
    const a = line(w); a.spacing = 3; for (const r of cd.rs.slice(0, 4)) ico(a, RICON[r], rc(r), 13);
    txt(a, grpName(cd) + (cd.t === 'out' ? ' out ' : ' in ') + clk(cd.at), 12.5, INK, true); a.addSpacer();
    timer(w, cd.at, 22);
    if (cd2) { const b = line(w); b.spacing = 3; txt(b, 'then', 11, MUTE); for (const r of cd2.rs.slice(0, 4)) ico(b, RICON[r], rc(r), 10); txt(b, grpName(cd2) + (cd2.t === 'out' ? ' out ' : ' in ') + clk(cd2.at), 11, MUTE); b.addSpacer(); }
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
  if (fam === 'accessoryInline' && view === 'count' && cd) { w.addText(cd.rs.join(' + ') + ' ' + cd.t + ' ' + clk(cd.at)); return w; }
  if (fam === 'accessoryRectangular' && view === 'tomorrow') {
    const T = j.tomorrow; if (!T || !T.first) { txt(w, 'Tomorrow', 13, INK, true); txt(w, T ? 'Nothing booked yet' : 'Update the engine', 12, MUTE); return w; }
    const a = line(w); a.spacing = 3; txt(a, 'Tomorrow · ' + T.n, 13, INK, true); for (const r of (T.rooms || []).slice(0, 5)) ico(a, RICON[r], rc(r), 11); a.addSpacer();
    const b = line(w); b.spacing = 3; txt(b, 'First ' + clk(T.first.at), 12, INK); ico(b, RICON[T.first.r], rc(T.first.r), 12); txt(b, T.first.r, 12, INK); b.addSpacer();
    txt(w, T.last ? 'Last out ' + clk(T.last) : '', 11, MUTE); return w;
  }
  if (fam === 'accessoryInline') {
    const e = j.nextEnd, s = j.nextStart, m = msgs.filter((q) => q.at > Date.now())[0], x = ev[0];
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
      const s = stateOf(one), m = (one.msgs || []).filter((q) => q.at > Date.now())[0];
      txt(w, one.r + ' ' + s.short, 13, INK, true);
      txt(w, [tvOf(one), one.ps4 && one.ps4.on ? ps4Of(one) : ''].filter(Boolean).join(' · ') || nextLine(one) || '–', 12, INK);
      txt(w, m ? '🔔 ' + clk(m.at) + ' ' + msgName(m) : nextLine(one) || (one.next && one.st !== 'next' ? 'next ' + clk(one.next) : ''), 11, MUTE);
      return w;
    }
    if (view === 'msgs') { const L = msgs.filter((q) => q.at > Date.now() - 5 * 60e3 && !q.ok).slice(0, 3); for (const m of L) txt(w, clk(m.at) + ' ' + m.r + ' · ' + (isAuto(m) ? (m.k === 'wrapup' ? '10 min' : 'wrap') : msgName(m)) + (mState(m) === 'wait' ? ' …' : ''), 12, INK, isAuto(m) ? '' : true); if (!L.length) txt(w, 'No TV messages waiting', 12, MUTE); return w; }
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
  w.backgroundColor = BG; w.setPadding(14, 14, 14, 14);
  { const g = new LinearGradient(); g.colors = [new Color('#15111f'), new Color('#0b0914')]; g.locations = [0, 1]; w.backgroundGradient = g; }
  const n = fam === 'small' ? 4 : fam === 'medium' ? 5 : 13;

  if (one) {   /* one room in detail, in its colour */
    w.backgroundColor = mix(one.r, one.st === 'in' ? 0.3 : 0.14);
    const s = stateOf(one), h = line(w); ico(h, RICON[one.r], rc(one.r), big ? 20 : 16); txt(h, one.r, big ? 20 : 16, rc(one.r), 'room'); h.addSpacer(); txt(h, clk(j.now), 10, MUTE, 'time');
    w.addSpacer(4); txt(w, s.big, big ? 16 : 13.5, s.c === MUTE ? INK : s.c, 'time'); if (one.st === 'in' && one.till) leftLine(w, one, big ? 14 : 12); if (names && s.who) it(w, 'user', MUTE, s.who, 11, INK, true);
    if (fam !== 'small' || one.st === 'in') { if (tvOf(one)) it(w, 'tv', MUTE, tvOf(one) + (one.tv && one.tv.at && !one.tv.off && fam !== 'small' ? ' · since ' + clk(one.tv.at) : ''), 11, tvTone(one)); if (ps4Of(one)) it(w, 'pad', MUTE, ps4Of(one), 11, INK); }
    w.addSpacer();
    { const A = (one.msgs || []).filter(isAuto), S = (one.msgs || []).filter((m) => !isAuto(m)); if (A.length) autoLine(w, A, 10.5); for (const m of S.slice(0, fam === 'small' ? 1 : 2)) schedLine(w, m, 10.5); }
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
  if (view === 'msgs') {   /* AUTO (10-min warning + That's a wrap, one row per room) and SCHEDULED, coloured by the TV's OK */
    const sz = fam === 'small' ? 10 : big ? 13 : 11.5, A = {}, S = msgs.filter((m) => !isAuto(m)), sm = fam === 'small';
    for (const m of msgs.filter(isAuto)) (A[m.r] = A[m.r] || []).push(m);
    const ar = W_ROOMS.filter((r) => A[r]);
    header(w, j, fam === 'small' ? 10 : 12, fam === 'small' ? 'MSGS' : 'TV MESSAGES'); w.addSpacer(5);
    const nA = fam === 'small' ? 2 : big ? 7 : 3, nS = fam === 'small' ? 2 : big ? 7 : Math.max(1, 4 - Math.min(nA, ar.length));
    if (ar.length) {
      plabel(w, fam === 'small' ? 'AUTO' : 'AUTO · 10-MIN & WRAP', MUTE, 6); w.addSpacer(3);
      for (const r of ar.slice(0, nA)) { autoLine(w, A[r], sz, (l) => { ico(l, RICON[r], rc(r), sz); if (!sm) { const nm = l.addStack(); nm.size = new Size(sz * 6.8, 0); txt(nm, r, sz, rc(r), 'room'); } }, sm); w.addSpacer(big ? 5 : 2); }
      w.addSpacer(big ? 10 : 5);
    }
    if (S.length) { plabel(w, 'SCHEDULED', MUTE, 6); w.addSpacer(3); for (const m of S.slice(0, nS)) { schedLine(w, m, sz, !sm, !sm); w.addSpacer(big ? 5 : 2); } }
    if (!ar.length && !S.length) txt(w, 'No TV messages tonight', 11, MUTE);
    w.addSpacer();
    if (fam !== 'small') { const k = line(w); k.spacing = 8; txt(k, '● coming', 9, INK); txt(k, '● on TV, waiting for OK', 9, WARN); txt(k, '● OK’d', 9, OK); k.addSpacer(); }
    return w;
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
  w.setPadding(11, 10, 11, 10);
  const foot = () => { const f = w.addStack(); f.layoutVertically(); f.setPadding(0, EDGE, 0, EDGE); return f; };
  if (fam === 'medium') {   /* every room as a small colour box: name, status, TV / PS4 */
    header(w, j, 11); w.addSpacer(4);
    boxGrid(w, open, open.length > 4 ? 32 : 50, false);
    w.addSpacer(); if (hl(j).list.length) warnLine(foot(), j, 9.5);
    return w;
  }
  header(w, j, 12); w.addSpacer(6);
  /* large: the rows share what's left after the header and the warning line */
  const nW = Math.min(2, Math.max(1, hl(j).list.length)), footH = 15 * nW + (j.money ? 16 : 0);
  boxGrid(w, open, widgetH() - 22 - 24 - footH - 6 * (Math.ceil(open.length / 2) - 1), true);
  w.addSpacer(); { const f = foot(); if (j.money) moneyLine(f, j, 10); warnLine(f, j, 10.5, 2); }
  return w;
}

const widget = await build();
if (config.runsInWidget) Script.setWidget(widget);
else await (fam === 'small' ? widget.presentSmall() : fam === 'medium' ? widget.presentMedium() : widget.presentLarge());
Script.complete();
