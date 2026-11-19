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

function clk(ms) { const d = new Date(ms + 8 * 3600e3), h = d.getUTCHours(), m = d.getUTCMinutes(); return ((h + 11) % 12 + 1) + (m ? ':' + String(m).padStart(2, '0') : '') + (h < 12 ? 'am' : 'pm'); }
function fnt(sz, bold) { return bold ? Font.boldSystemFont(sz) : Font.systemFont(sz); }
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
  const h = line(w);
  txt(h, 'ROUND2', sz, ACC, true);
  const c = j.counts || {}; txt(h, label || (c.in + ' in · ' + c.left + ' to come'), sz, INK); h.addSpacer();
  txt(h, (j.offline ? 'offline · ' : '') + clk(j.now), sz - 2, j.offline ? WARN : MUTE);
}
function warnLine(w, j, sz, n) {
  const L = j.warn || [];
  if (!L.length) { txt(w, '✓ All TVs reporting', sz, OK); return; }
  for (const x of L.slice(0, n || 1)) txt(w, '⚠︎ ' + x, sz, WARN);
}
function bar(parent, r, h) { const b = parent.addStack(); b.size = new Size(4, h); b.backgroundColor = rc(r.r); b.cornerRadius = 2; }
/* board row: Room · state · guest / TV · PS4 · next TV message */
function boardRow(parent, r, wide) {
  const s = stateOf(r), row = line(parent); row.spacing = 6; bar(row, r, wide ? 38 : 24);
  const col = row.addStack(); col.layoutVertically();
  const top = line(col);
  txt(top, r.r, wide ? 13 : 11, rc(r.r), true);
  txt(top, wide ? s.big : s.short, wide ? 13 : 11, s.c, r.st === 'in');
  if (wide && names && s.who) txt(top, s.who, 12, MUTE);
  const m = (r.msgs || [])[0];
  const a = [tvOf(r) && '📺 ' + tvOf(r), r.ps4 && r.ps4.on && '🎮 ' + ps4Of(r)].filter(Boolean).join('  ');
  const b = wide ? [m && '🔔 ' + clk(m.at) + ' ' + msgName(m), nextLine(r)].filter(Boolean).join('  ·  ') : '';
  if (a) txt(col, a, wide ? 10.5 : 9, r.tv && (r.tv.off || r.tv.quiet) ? WARN : MUTE);
  if (b) txt(col, b, 10.5, MUTE);
  if (wide) row.addSpacer();
}
/* tv row: Room · TV app (since) · PS4 game */
function tvRow(parent, r, wide) {
  const row = line(parent); bar(row, r, wide ? 20 : 16);
  txt(row, r.r, wide ? 12 : 10.5, rc(r.r), true);
  const t = tvOf(r) || '–'; txt(row, '📺 ' + t + (wide && r.tv && r.tv.at && !r.tv.off ? ' · since ' + clk(r.tv.at) : ''), wide ? 12 : 10.5, r.tv && (r.tv.off || r.tv.quiet) ? WARN : INK);
  const p = ps4Of(r); if (p) txt(row, '🎮 ' + p, wide ? 12 : 10.5, r.ps4 && r.ps4.on ? INK : MUTE);
  row.addSpacer();
}
function timeRow(parent, at, room, what, sz) {
  const row = line(parent); bar(row, { r: room }, sz + 6);
  const tm = row.addStack(); tm.size = new Size(sz * 4.6, 0); txt(tm, clk(at), sz, INK, true);
  txt(row, room, sz, rc(room), true); txt(row, what, sz, MUTE); row.addSpacer();
}

/* ---------- widgets ---------- */
async function build() {
  const j = await load(), w = new ListWidget();
  w.url = CC + '?tab=' + (view === 'tv' ? 'tvs' : view === 'msgs' ? 'tonight' : 'insession');
  if (j.error) { if (!lock) w.backgroundColor = BG; txt(w, 'Round2', 13, ACC, true); txt(w, j.error, 11, WARN).lineLimit = 3; return w; }
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
  const n = fam === 'small' ? 4 : fam === 'medium' ? 5 : 13;

  if (one) {   /* one room in detail, in its colour */
    const s = stateOf(one), h = line(w); bar(h, one, 18); txt(h, one.r, big ? 18 : 15, rc(one.r), true); h.addSpacer(); txt(h, clk(j.now), 10, MUTE);
    w.addSpacer(4); txt(w, s.big, big ? 15 : 13, s.c, true); if (names && s.who) txt(w, s.who, 11, MUTE);
    if (fam !== 'small' || one.st === 'in') { if (tvOf(one)) txt(w, '📺 ' + tvOf(one) + (one.tv && one.tv.at && !one.tv.off && fam !== 'small' ? ' · since ' + clk(one.tv.at) : ''), 11, INK); if (ps4Of(one)) txt(w, '🎮 ' + ps4Of(one), 11, INK); }
    w.addSpacer();
    for (const m of (one.msgs || []).slice(0, fam === 'small' ? 1 : 3)) txt(w, '🔔 ' + clk(m.at) + ' ' + msgName(m), 10.5, MUTE);
    const nl = nextLine(one); if (nl) txt(w, nl, 10.5, MUTE);
    return w;
  }

  if (view === 'msgs') {   /* TV messages coming up, every room, in time order */
    header(w, j, fam === 'small' ? 10 : 12, fam === 'small' ? 'TV msgs' : 'TV messages coming up'); w.addSpacer(6);
    for (const m of msgs.slice(0, n)) { timeRow(w, m.at, m.r, msgName(m), fam === 'small' ? 10 : 11.5); w.addSpacer(3); }
    if (!msgs.length) txt(w, 'Nothing scheduled', 11, MUTE);
    w.addSpacer(); return w;
  }
  if (view === 'times') {   /* tonight's starts and ends still to come */
    header(w, j, fam === 'small' ? 10 : 12, fam === 'small' ? 'Tonight' : c.in + ' in · ' + c.left + ' to come'); w.addSpacer(6);
    for (const x of ev.slice(0, n)) { timeRow(w, x.at, x.r, evWhat(x), fam === 'small' ? 10 : 11.5); w.addSpacer(3); }
    if (!ev.length) txt(w, 'Nothing else tonight', 11, MUTE);
    w.addSpacer(); return w;
  }
  if (view === 'tv') {   /* what's on each TV and PS4 */
    header(w, j, fam === 'small' ? 10 : 12, 'TVs & PS4s'); w.addSpacer(6);
    for (const r of open) { tvRow(w, r, big); w.addSpacer(big ? 6 : 2); }
    w.addSpacer(); if (fam !== 'small') warnLine(w, j, 10); return w;
  }

  /* board (default) */
  if (fam === 'small') {   /* how busy, and the next two things that happen */
    txt(w, 'ROUND2', 11, ACC, true); w.addSpacer(2);
    const bg = line(w); bg.bottomAlignContent(); txt(bg, String(c.in), 34, INK, true); txt(bg, ' / ' + c.open + ' in', 13, MUTE);
    txt(w, c.done + ' done · ' + c.left + ' to come', 11, MUTE); w.addSpacer();
    const e = j.nextEnd, s = j.nextStart;
    for (const [lab, x] of [['Out', e], ['In', s]]) if (x) { const l = line(w); l.spacing = 3; txt(l, lab, 11, MUTE); txt(l, x.r, 11, rc(x.r), true); const d = l.addDate(new Date(x.at)); d.applyRelativeStyle(); d.font = fnt(11); d.textColor = INK; }
    if ((j.warn || []).length) txt(w, '⚠︎ ' + j.warn[0], 10, WARN);
    return w;
  }
  if (fam === 'medium') {   /* every room at a glance, two columns */
    header(w, j, 12); w.addSpacer(6);
    const cols = line(w); cols.spacing = 10; cols.topAlignContent();
    const a = cols.addStack(), b = cols.addStack(); a.layoutVertically(); b.layoutVertically(); a.spacing = 5; b.spacing = 5;
    open.forEach((r, i) => boardRow(i < Math.ceil(open.length / 2) ? a : b, r, false));
    w.addSpacer(); warnLine(w, j, 10);
    return w;
  }
  header(w, j, 13); w.addSpacer(8);
  for (const r of open) { boardRow(w, r, true); w.addSpacer(6); }
  w.addSpacer(); warnLine(w, j, 11, 3);
  return w;
}

const widget = await build();
if (config.runsInWidget) Script.setWidget(widget);
else await (fam === 'small' ? widget.presentSmall() : fam === 'medium' ? widget.presentMedium() : widget.presentLarge());
Script.complete();
