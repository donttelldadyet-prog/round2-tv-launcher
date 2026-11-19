// Round2 – iPhone widgets (Scriptable app). Made by the control centre › Tests & systems › Phone alerts & widgets (20 Nov 2026).
// Home screen: hold an empty spot → Edit → Add widget → Scriptable → pick a size → tap the new widget → Script: Round2.
// Lock screen: hold the lock screen → Customize → Lock Screen → tap the widget row → Scriptable → tap it → Script: Round2.
// Optional "Parameter" on a widget:  nonames  (hide guest names)   room:Kai  (small / lock screen: just that room)
// Data refreshes every few minutes (iOS decides exactly when). Tap a widget to open the control centre.
const KEY = '__KEY__', API = '__URL__', CC = 'https://staff.round2.sg/';
const RC = { Vale: '#E8436A', Kaeden: '#4DB848', Scarlett: '#F2C21B', Bella: '#7A5BE0', Kai: '#24B49A', Atlas: '#E9E7EF', Max: '#E0402F' };
const BG = new Color('#0b0914'), PANEL = new Color('#17121f'), INK = new Color('#f4f1fb'), MUTE = new Color('#8d84a6'), ACC = new Color('#F2C21B'), WARN = new Color('#ff9f43'), OK = new Color('#4DB848');
const fam = config.widgetFamily || 'large';
const par = String(args.widgetParameter || '').toLowerCase();
const lock = /^accessory/.test(fam), names = !/nonames/.test(par) && !lock;
const only = (par.match(/room:\s*([a-z]+)/) || [])[1];

function clk(ms) { const d = new Date(ms + 8 * 3600e3), h = d.getUTCHours(), m = d.getUTCMinutes(); return ((h + 11) % 12 + 1) + (m ? ':' + String(m).padStart(2, '0') : '') + (h < 12 ? 'am' : 'pm'); }
function fnt(sz, bold) { return bold ? Font.boldSystemFont(sz) : Font.systemFont(sz); }
function txt(st, s, sz, color, bold) { const t = st.addText(String(s)); t.font = fnt(sz, bold); t.textColor = color || INK; t.lineLimit = 1; t.minimumScaleFactor = 0.6; return t; }

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

/* one line per room: what's happening and the next thing to know */
function stateOf(r) {
  if (r.st === 'in') return { big: 'till ' + clk(r.till), who: r.n, c: OK };
  if (r.st === 'clean') return { big: 'cleaning till ' + clk(r.till), who: '', c: ACC };
  if (r.st === 'next') return { big: 'next ' + clk(r.next), who: r.nn, c: MUTE };
  if (r.st === 'closed') return { big: 'closed', who: '', c: MUTE };
  return { big: 'free', who: '', c: MUTE };
}
function nextLine(r) {   /* after the current guests: who comes next, and how long the room has in between */
  if (!r.next || r.st === 'next') return '';
  return 'then ' + clk(r.next) + (r.gap != null && r.gap <= 30 ? ' · ' + r.gap + ' min turnover' : '');
}
/* when to look again: the next change on the board (an end, a start, cleaning done), at least 2 min, at most 15 */
function refreshAt(j) {
  let t = Date.now() + 15 * 60e3;
  for (const r of j.rooms || []) for (const x of [r.till, r.next]) if (x && x > Date.now() && x < t) t = x + 30e3;
  return new Date(Math.max(t, Date.now() + 2 * 60e3));
}

function roomRow(parent, r, wide) {
  const s = stateOf(r), row = parent.addStack(); row.layoutHorizontally(); row.centerAlignContent(); row.spacing = 6;
  const bar = row.addStack(); bar.size = new Size(4, wide ? 30 : 22); bar.backgroundColor = new Color(RC[r.r] || '#888'); bar.cornerRadius = 2;
  const col = row.addStack(); col.layoutVertically();
  const top = col.addStack(); top.layoutHorizontally(); top.spacing = 6;
  txt(top, r.r, wide ? 13 : 11, INK, true);
  txt(top, s.big, wide ? 13 : 11, s.c, r.st === 'in');
  const sub = [names && s.who ? s.who : '', wide ? nextLine(r) : ''].filter(Boolean).join(' · ');
  if (sub) txt(col, sub, wide ? 11 : 9, MUTE);
  if (wide) row.addSpacer();
}

function header(w, j, sz) {
  const h = w.addStack(); h.layoutHorizontally(); h.centerAlignContent();
  txt(h, 'ROUND2', sz, ACC, true); h.addSpacer(6);
  const c = j.counts || {}; txt(h, c.in + ' in · ' + c.left + ' to come', sz, INK); h.addSpacer();
  txt(h, (j.offline ? 'offline · ' : '') + clk(j.now), sz - 2, j.offline ? WARN : MUTE);
}
function warnLine(w, j, sz) {
  const L = j.warn || [];
  if (!L.length) { txt(w, '✓ All TVs reporting', sz, OK); return; }
  for (const x of L.slice(0, fam === 'large' ? 4 : 1)) txt(w, '⚠︎ ' + x, sz, WARN);
}

async function build() {
  const j = await load(), w = new ListWidget();
  w.url = CC + '?tab=insession';
  if (j.error) { if (!lock) w.backgroundColor = BG; txt(w, 'Round2', 13, ACC, true); txt(w, j.error, 11, WARN).lineLimit = 3; return w; }
  w.refreshAfterDate = refreshAt(j);
  const c = j.counts || {}, rooms = (j.rooms || []).filter((r) => !only || r.r.toLowerCase() === only);

  if (fam === 'accessoryInline') {   /* the line next to the clock */
    const e = j.nextEnd, s = j.nextStart;
    w.addText(only && rooms[0] ? rooms[0].r + ' ' + stateOf(rooms[0]).big
      : e && (!s || e.at <= s.at) ? e.r + ' out ' + clk(e.at) + (s ? ' · ' + s.r + ' in ' + clk(s.at) : '')
      : s ? s.r + ' in ' + clk(s.at) + ' · ' + c.in + ' in now' : c.in + ' in · nothing else tonight');
    return w;
  }
  if (fam === 'accessoryCircular') {   /* rooms in use now, out of the open rooms */
    w.addAccessoryWidgetBackground = true;
    const st = w.addStack(); st.layoutVertically(); st.centerAlignContent();
    const a = st.addStack(); a.addSpacer(); txt(a, only && rooms[0] ? rooms[0].r.slice(0, 4) : c.in + '/' + c.open, 18, INK, true); a.addSpacer();
    const b = st.addStack(); b.addSpacer(); txt(b, only && rooms[0] ? (rooms[0].st === 'in' ? clk(rooms[0].till) : rooms[0].st) : (j.warn || []).length ? '⚠︎ ' + j.warn.length : 'IN', 10, MUTE); b.addSpacer();
    return w;
  }
  if (fam === 'accessoryRectangular') {   /* three lines on the lock screen */
    if (only && rooms[0]) { const r = rooms[0], s = stateOf(r); txt(w, r.r, 13, INK, true); txt(w, s.big, 12, INK); txt(w, nextLine(r) || (r.next && r.st !== 'next' ? 'next ' + clk(r.next) : ''), 11, MUTE); return w; }
    txt(w, 'R2 · ' + c.in + ' in · ' + c.left + ' to come', 13, INK, true);
    const e = j.nextEnd, s = j.nextStart;
    txt(w, e ? 'Out ' + clk(e.at) + ' · ' + e.r : 'Nobody in now', 12, INK);
    txt(w, (j.warn || []).length ? '⚠︎ ' + j.warn[0] : s ? 'In ' + clk(s.at) + ' · ' + s.r : 'No more sessions tonight', 12, INK);
    return w;
  }

  w.backgroundColor = BG; w.setPadding(12, 12, 12, 12);
  if (fam === 'small') {   /* how busy, and the next two things that happen */
    if (only && rooms[0]) {
      const r = rooms[0], s = stateOf(r); txt(w, r.r, 15, new Color(RC[r.r] || '#fff'), true); w.addSpacer(4); txt(w, s.big, 13, s.c, true);
      if (names && s.who) txt(w, s.who, 11, MUTE); w.addSpacer();
      const nl = nextLine(r) || (r.next && r.st !== 'next' ? 'next ' + clk(r.next) : ''); if (nl) txt(w, nl, 11, MUTE);
      return w;
    }
    txt(w, 'ROUND2', 11, ACC, true); w.addSpacer(2);
    const big = w.addStack(); big.layoutHorizontally(); big.bottomAlignContent(); txt(big, String(c.in), 34, INK, true); txt(big, ' / ' + c.open + ' in', 13, MUTE);
    txt(w, c.done + ' done · ' + c.left + ' to come', 11, MUTE); w.addSpacer();
    const e = j.nextEnd, s = j.nextStart;
    if (e) { const l = w.addStack(); l.layoutHorizontally(); txt(l, 'Out ' + e.r + ' ', 11, INK, true); const d = l.addDate(new Date(e.at)); d.applyRelativeStyle(); d.font = fnt(11); d.textColor = INK; }
    if (s) { const l = w.addStack(); l.layoutHorizontally(); txt(l, 'In ' + s.r + ' ', 11, INK, true); const d = l.addDate(new Date(s.at)); d.applyRelativeStyle(); d.font = fnt(11); d.textColor = INK; }
    if ((j.warn || []).length) txt(w, '⚠︎ ' + j.warn[0], 10, WARN);
    return w;
  }
  if (fam === 'medium') {   /* every room at a glance, two columns */
    header(w, j, 12); w.addSpacer(6);
    const cols = w.addStack(); cols.layoutHorizontally(); cols.spacing = 10;
    const L = rooms.filter((r) => r.st !== 'closed'), a = cols.addStack(), b = cols.addStack(); a.layoutVertically(); b.layoutVertically(); a.spacing = 4; b.spacing = 4;
    L.forEach((r, i) => roomRow(i < Math.ceil(L.length / 2) ? a : b, r, false));
    w.addSpacer(); warnLine(w, j, 10);
    return w;
  }
  /* large (and extra large): every room with who's in, when they leave, who's next and the turnover; all warnings */
  header(w, j, 13); w.addSpacer(8);
  for (const r of rooms.filter((x) => x.st !== 'closed')) { roomRow(w, r, true); w.addSpacer(5); }
  w.addSpacer();
  const e = j.nextEnd, s = j.nextStart, f = w.addStack(); f.layoutHorizontally();
  txt(f, (e ? 'Next out ' + e.r + ' ' + clk(e.at) : 'Nobody in') + (s ? '   ·   next in ' + s.r + ' ' + clk(s.at) : ''), 11, INK);
  w.addSpacer(4); warnLine(w, j, 11);
  return w;
}

const widget = await build();
if (config.runsInWidget) Script.setWidget(widget);
else await widget.presentLarge();
Script.complete();
