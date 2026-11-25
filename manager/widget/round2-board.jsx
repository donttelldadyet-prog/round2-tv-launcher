// Round2 board – a desktop widget for the Mac (Übersicht).  Owner, 25 Nov 2026: "lets try 2, i bought a new macbook pro 16".
// Look (25 Nov, owner: "our design style can be felt from our Round2 logo … we only have white on black and vice versa,
// colours are our product but our main feel is monochrome"): drawn like the logo – thin rounded lines, outlines not fills,
// soft small corners, wide letter-spacing, no gradients or glass. Monochrome (dark: white on black · light: black on white,
// the logo too); each room's colour only as its icon, a dot and a thin line when guests are in. Text in Quicksand (a thin
// rounded monoline like the logo's strokes); Press Start 2P only for the small section labels.
// Get it from the control centre (Phone alerts & widgets › Download Mac board): this shop's widget key is filled in.
// Put the file in Übersicht's widgets folder (Übersicht menu-bar icon › Open Widgets Folder). Same live data as the iPhone
// widgets (the engine's /w), checked every 30 s; countdowns tick every second on the engine's clock (Singapore time).
//
// MOVE / RESIZE / ROOM DETAILS (25 Nov, owner: "can i resize it or move the widget … if i double click scarlett can it show
// me that"): needs Übersicht › Preferences › Interaction Shortcut (e.g. ⌘) – then, while HOLDING that key:
//   drag the top row (logo / Tonight) to move the board · drag the corner mark at the bottom right to resize it ·
//   double-click that corner mark to put it back · double-click a room for its details (Esc / ✕ / double-click to close).
// Where you put it is remembered on this Mac and kept when you download a new version.
// Starting place (used until you move it):
const SIDE = 'left';          // 'left' or 'right' – macOS keeps desktop icons on the right
const TOP = 46;               // points from the top (the MacBook's menu bar + notch are ~37)
const EDGE = 24;              // points from the side
const SCALE = 1;              // 1 = made for the 16" MacBook Pro's default size (1728 × 1117); 0.85 smaller, 1.15 bigger
const NAMES = true;           // false = hide guest names
// The strip under the rooms – up to 5, in this order (or tap "Choose" on the board): next · today · tomorrow · health ·
// free (rooms free now, for walk-ins) · msgs (all TV messages) · screens (TVs & PS4s) · turn (turnovers) · night (the night)
const STRIP = ['next', 'today', 'tomorrow', 'health'];
const MODE = 'auto';          // starting look: 'dark', 'light' or 'auto' (follows the Mac) – or tap the ◐ switch on the board

const KEY = '__KEY__', API = '__URL__';
const FONTS = 'https://staff.round2.sg/widget/fonts/';

import { React, run } from 'uebersicht';

const RC = { Vale: '#E8436A', Kaeden: '#4DB848', Scarlett: '#F2C21B', Bella: '#7A5BE0', Kai: '#24B49A', Atlas: '#E9E7EF', Max: '#E0402F' };
const INK = '#f4f1fb', MUTE = '#8d84a6', ACC = '#F2C21B', OK = '#4DB848', WARN = '#ff9f43', BAD = '#ff4d5e';
const RICON = { Vale: 'cat', Kaeden: 'leaf', Scarlett: 'saturn', Kai: 'mount', Bella: 'book', Atlas: 'boat', Max: 'car' };
const PIX = {"tv":["..#.....#...","...#...#....","....#.#.....","############","#..........#","#...#......#","#...###....#","#...#####..#","#...###....#","#...#......#","#..........#","############"],"clock":["...######...","..#......#..",".#...#....#.","#....#.....#","#....#.....#","#....###...#","#.......#..#","#........#.#","#..........#",".#........#.","..#......#..","...######..."],"user":["....####....","...#....#...","...#....#...","...#....#...","....####....","............","..########..",".#........#.","#..........#","#..........#","#..........#","############"],"cake":[".....##.....","......#.....",".....##.....",".....##.....",".##########.",".#........#.",".#.##..##.#.",".##########.","#..........#","#.#.#..#.#.#","#..........#","############"],"cat":["............",".#.......#..",".##.....##..",".#.#####.#..",".#.......#..","#..#...#..#.","#.........#.","#....#....#.",".#..#.#..#..","..#.....#...","...#####....","............"],"mount":["............","............",".....#......","....#.#.....","...#...#.#..","..#.#.#.#.#.","..#..#..#..#",".#.........#",".#.........#","#...........","############","............"],"book":["............",".####..####.","#....##....#","#.##.##.##.#","#....##....#","#.##.##.##.#","#....##....#","#.##.##.##.#","#....##....#",".####..####.",".....##.....","............"],"boat":[".....#......",".....##.....",".....###....",".....####...",".....#####..",".....######.",".....#......","############",".##########.","..########..","............","#.##.##.##.#"],"car":["............","............","...######...","..#..#...#..",".#...#....#.","############","#..........#","#.##....##.#","############",".##......##.",".##......##.","............"],"saturn":["..............","..............",".....####.....","....######..##","...#########.#","...#######.##.","...#####..#...","...##...###...",".##..######...","#.#########...","##..######....",".....####.....","..............",".............."],"leaf":[".........###","......######","....#####..#","...####...#.","..####...#..","..###...#...",".###...#....",".##...#.....",".#.###......",".###........","#...........","............"],"moon":["....####....","..###.......",".##.........",".#..........","##..........","##..........","##..........","##.........#",".##.......##",".###.....##.","..########..","....####...."],"pad":["............","..########..",".#........#.","#..#....#..#","#.###..#.#.#","#..#....#..#","#..........#","#...####...#",".#.#....#.#.","..#......#..","............","............"],"bell":[".....##.....","....####....","...######...","...######...","..########..","..########..",".##########.","############","............",".....##.....","............","............"],"warn":[".....##.....","....####....","....#..#....","...##..##...","...##..##...","..###..###..","..###..###..",".##########.",".####..####.","#####..#####","############","............"],"check":["............","...........#","..........##",".........##.","#.......##..","##.....##...",".##...##....","..##.##.....","...###......","....#.......","............","............"],"sparkle":["......#.....","......#.....",".....###....","###########.",".....###....","......#.....","..#...#.....","..#.........",".###......#.","..#......###","..#.......#.","............"]};
const LOGO = 'data:image/png;base64,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';

/* ---------- data: every 30 s; the screen ticks every second ---------- */
export const refreshFrequency = 5000;   /* minutes only now (25 Nov) – no need to redraw every second */
let last = 0, busy = false;
export const command = async (dispatch) => {
  dispatch({ type: 'TICK' });
  if (busy || Date.now() - last < 30000) return;
  busy = true;
  try {
    const t0 = Date.now(), r = await fetch(API + '?k=' + encodeURIComponent(KEY), { cache: 'no-store' }), j = await r.json();
    last = Date.now();
    if (j && j.ok) dispatch({ type: 'DATA', j, off: j.now - (t0 + last) / 2 }); else dispatch({ type: 'ERR', e: (j && j.error) || 'no answer' });
  } catch (e) { last = Date.now() - 20000; dispatch({ type: 'ERR', e: 'offline – retrying' }); }
  busy = false;
};
/* where the board sits and how big – remembered on this Mac (Übersicht keeps the browser storage) */
const SAVE = 'round2-board-place';
const saved = (() => { try { return JSON.parse(localStorage.getItem(SAVE) || '{}') || {}; } catch (e) { return {}; } })();
const keep = (s) => { try { localStorage.setItem(SAVE, JSON.stringify({ pos: s.pos, scale: s.scale, strip: s.strip, mode: s.mode })); } catch (e) {} return s; };
export const initialState = { j: null, off: 0, err: '', at: 0, tick: 0, pos: saved.pos || null, scale: saved.scale || SCALE, open: '', strip: Array.isArray(saved.strip) && saved.strip.length ? saved.strip : STRIP, pick: false, mode: ['dark', 'light', 'auto'].includes(saved.mode) ? saved.mode : MODE };
export const updateState = (ev, s) => {
  if (ev.type === 'DATA') return { ...s, j: ev.j, off: ev.off, err: '', at: Date.now() };
  if (ev.type === 'ERR') return { ...s, err: ev.e };
  if (ev.type === 'TICK') return { ...s, tick: s.tick + 1 };
  if (ev.type === 'PLACE') return ev.done ? keep({ ...s, pos: ev.pos || s.pos, scale: ev.scale || s.scale }) : { ...s, pos: ev.pos || s.pos, scale: ev.scale || s.scale };
  if (ev.type === 'RESET') return keep({ ...s, pos: null, scale: SCALE });
  if (ev.type === 'MODE') return keep({ ...s, mode: { dark: 'light', light: 'auto', auto: 'dark' }[s.mode || MODE] || 'dark' });
  if (ev.type === 'PICK') return { ...s, pick: !s.pick };
  if (ev.type === 'STRIP') return keep({ ...s, strip: ev.list.length ? ev.list : STRIP });
  if (ev.type === 'OPEN') return { ...s, open: s.open === ev.r ? '' : ev.r };
  return s;
};

/* ---------- the engine's clock, Singapore time ---------- */
const now = (s) => Date.now() + (s.off || 0);
const sgd = (ms) => new Date(ms + 8 * 3600e3);
const clk = (ms) => { const d = sgd(ms), h = d.getUTCHours(), m = d.getUTCMinutes(); return ((h + 11) % 12 + 1) + (m ? ':' + String(m).padStart(2, '0') : '') + (h < 12 ? 'am' : 'pm'); };
const clkFull = (ms) => { const d = sgd(ms), h = d.getUTCHours(); return ((h + 11) % 12 + 1) + ':' + String(d.getUTCMinutes()).padStart(2, '0') + (h < 12 ? 'am' : 'pm'); };
const dayName = (ms) => { const d = sgd(ms); return ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getUTCDay()] + ' ' + d.getUTCDate() + ' ' + ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][d.getUTCMonth()]; };
const left = (ms) => { const t = Math.max(0, Math.round(ms / 1000)), h = Math.floor(t / 3600), m = Math.floor(t % 3600 / 60), x = t % 60; return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(x).padStart(2, '0'); };
const ago = (t, n) => { const m = Math.round((n - t) / 60e3); return m < 1 ? 'just now' : m < 60 ? m + ' min ago' : Math.round(m / 60) + 'h ago'; };
const money = (v) => 'S$' + (Math.round(+v * 100) / 100).toFixed(2).replace(/\.00$/, '');
const mix = (hex, a) => { const n = parseInt(hex.slice(1), 16); return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`; };

/* ---------- monochrome, like the logo ---------- */
const lightNow = (mode = MODE) => mode === 'light' || (mode === 'auto' && typeof window !== 'undefined' && window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches);
const themeOf = (L) => L ? { bg: '#fafafa', fg: '#0a0a0a', sub: 'rgba(10,10,10,.52)', line: 'rgba(10,10,10,.13)', line2: 'rgba(10,10,10,.28)', ok: '#2e8b36', warn: '#d9731a', acc: '#b38a00', bad: '#d13348', logo: 'invert(1)',
  room: (r) => ({ Atlas: '#7d7a8c', Scarlett: '#c79a00' })[r] || RC[r] }
  : { bg: '#050505', fg: '#f5f5f5', sub: 'rgba(245,245,245,.5)', line: 'rgba(245,245,245,.14)', line2: 'rgba(245,245,245,.3)', ok: '#4DB848', warn: '#ff9f43', acc: '#F2C21B', bad: '#ff4d5e', logo: 'none', room: (r) => RC[r] };
let K = themeOf(false);

const Pix = ({ name, color, size = 14 }) => {
  const P = PIX[name]; if (!P) return null;
  const rects = []; P.forEach((row, y) => { for (let x = 0; x < row.length; x++) if (row[x] === '#') rects.push(<rect key={x + '-' + y} x={x} y={y} width="1.02" height="1.02" />); });
  return <svg width={size} height={size * P.length / P[0].length} viewBox={`0 0 ${P[0].length} ${P.length}`} fill={color} style={{ display: 'block', flex: 'none', shapeRendering: 'crispEdges' }}>{rects}</svg>;
};
const Kick = ({ children, color, size = 7 }) => <div style={{ fontFamily: 'PS2P', fontSize: size, color: color || K.sub, letterSpacing: '.28em', textTransform: 'uppercase', whiteSpace: 'nowrap' }}>{children}</div>;
const Dot = ({ c, size = 6 }) => <i style={{ width: size, height: size, borderRadius: '50%', background: c, display: 'inline-block', flex: 'none' }} />;
/* a status chip drawn like the logo's II bars: a thin outline with small round corners; the state is only the dot's colour */
const Chip = ({ c, children, sm, strong, fill, dim }) => (
  <span style={{ display: 'inline-flex', alignItems: 'center', gap: sm ? 6 : 8, padding: sm ? '3px 9px' : '6px 12px', borderRadius: 6, border: `1px solid ${strong ? c : K.line2}`, background: fill || 'transparent', opacity: dim ? 0.85 : 1,
    color: K.fg, fontWeight: 600, fontSize: sm ? 11.5 : 13, whiteSpace: 'nowrap', maxWidth: '100%', overflow: 'hidden', fontVariantNumeric: 'tabular-nums' }}>
    {c && <Dot c={c} size={sm ? 5 : 6} />}<span style={{ overflow: 'hidden', textOverflow: 'ellipsis', minWidth: 0 }}>{children}</span></span>);
const Line = ({ icon, children, bold, color }) => (
  <div style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 0, fontWeight: bold ? 600 : 500, color: color || (bold ? K.fg : K.sub) }}>
    {icon && <Pix name={icon} color={color || K.sub} size={11} />}<span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', minWidth: 0 }}>{children}</span></div>);
/* a card: just a thin rounded outline; when guests are in, the outline takes the room's colour */
const Card = ({ color, lit, children, style, onDoubleClick, title }) => (
  <div title={title} onDoubleClick={onDoubleClick} style={{ borderRadius: 12, padding: '15px 16px 16px', border: `1px solid ${lit ? color : K.line}`, ...style }}>{children}</div>);
/* a button in the same thin-outline style */
const Btn = ({ children, onClick, c }) => <span role="button" onClick={onClick} style={{ display: 'inline-flex', alignItems: 'center', gap: 6, padding: '6px 12px', borderRadius: 6,
  border: `1px solid ${c || K.line2}`, fontWeight: 600, fontSize: 12.5, cursor: 'pointer', whiteSpace: 'nowrap' }}>{children}</span>;

/* ---------- moving and resizing (hold Übersicht's interaction key) ---------- */
let drag = null;
const startDrag = (e, s, d, mode) => {
  if (e.button !== 0) return; e.preventDefault(); e.stopPropagation();
  const b = e.currentTarget.closest('[data-board]').getBoundingClientRect();
  drag = { mode, x0: e.clientX, y0: e.clientY, left: b.left, top: b.top, w: b.width, sc: s.scale };
  const mv = (ev) => { if (!drag) return; const dx = ev.clientX - drag.x0, dy = ev.clientY - drag.y0;
    if (drag.mode === 'move') d({ type: 'PLACE', pos: { x: Math.round(drag.left + dx), y: Math.max(0, Math.round(drag.top + dy)) } });
    else d({ type: 'PLACE', pos: { x: Math.round(drag.left), y: Math.round(drag.top) }, scale: Math.max(0.5, Math.min(1.8, Math.round(drag.sc * (drag.w + dx) / drag.w * 1000) / 1000)) }); };
  const up = (ev) => { mv(ev); drag = null; d({ type: 'PLACE', done: 1 }); window.removeEventListener('mousemove', mv); window.removeEventListener('mouseup', up); };
  window.addEventListener('mousemove', mv); window.addEventListener('mouseup', up);
};
let escD = null;
if (typeof window !== 'undefined' && !window.__r2esc) { window.__r2esc = 1; window.addEventListener('keydown', (e) => { if (e.key === 'Escape' && escD) escD({ type: 'OPEN', r: '' }); }); }
const Col = ({ title, first, children }) => <div style={{ display: 'grid', gap: 9, alignContent: 'start', minWidth: 0, padding: first ? '0 20px 0 4px' : '0 20px', borderLeft: first ? 'none' : `1px solid ${K.line}` }}><Kick>{title}</Kick>{children}</div>;
const Group = ({ title, children }) => <Card style={{ padding: '14px 16px 15px', display: 'grid', gap: 10 }}><Kick>{title}</Kick>{children}</Card>;

/* TV messages: automatic (10-min warning, That's a wrap) vs scheduled; dot: grey coming · orange on the TV waiting for OK ·
   green OK'd */
const isAuto = (m) => m.k === 'wrapup' || m.k === 'timeup';
const mState = (m, n) => m.ok ? 'ok' : m.at > n || m.ak === 0 ? 'next' : 'wait';
const mC = (m, n) => ({ ok: K.ok, wait: K.warn, next: K.sub })[mState(m, n)];

/* the session ring: a thin rounded line like the logo's strokes, in the room's colour – how much of the booking is used */
const Ring = ({ p, col, size = 92, children }) => {
  const R = size / 2 - 3, C = 2 * Math.PI * R, q = Math.max(0, Math.min(1, p));
  return <div style={{ position: 'relative', width: size, height: size, flex: 'none' }}>
    <svg width={size} height={size} style={{ display: 'block', transform: 'rotate(-90deg)' }}>
      <circle cx={size / 2} cy={size / 2} r={R} fill="none" stroke={K.line} strokeWidth="2" />
      {q > 0 && <circle cx={size / 2} cy={size / 2} r={R} fill="none" stroke={col} strokeWidth="2.5" strokeLinecap="round" strokeDasharray={`${C * q} ${C}`} />}
    </svg>
    <div style={{ position: 'absolute', inset: 0, display: 'grid', placeItems: 'center', alignContent: 'center', gap: 5, textAlign: 'center' }}>{children}</div></div>;
};
/* booking length for the ring: bookings go in 15-minute steps, so 2h 30m reads "2½ hr" */
const hrs = (ms) => { const m = Math.round(ms / 60e3), h = Math.floor(m / 60), x = m % 60, q = { 15: '¼', 30: '½', 45: '¾' }[x];
  return !x ? h + ' hr' : q && h ? h + q + ' hr' : (h ? h + ' hr ' : '') + x + ' min'; };
/* (25 Nov, owner: "omit the seconds countdown … make it know what the numbers mean like 1 hr 30 mins or 36 mins left") –
   whole minutes, rounded up (so "1 min" until it's really over) */
const mins = (ms) => Math.max(0, Math.ceil(ms / 60e3));
const durTxt = (ms) => { const t = mins(ms), h = Math.floor(t / 60), m = t % 60; return h ? h + ' hr' + (m ? ' ' + m + ' min' : '') : m + ' min'; };
const Dur = ({ ms, size = 36, color }) => { const t = mins(ms), h = Math.floor(t / 60), m = t % 60, u = { fontSize: Math.round(size * 0.4), fontWeight: 500, color: K.sub, marginLeft: 3, marginRight: 8, letterSpacing: '.02em' };
  return <span style={{ fontWeight: 300, fontSize: size, lineHeight: 1, color: color || K.fg, fontVariantNumeric: 'tabular-nums', whiteSpace: 'nowrap' }}>
    {h > 0 && <>{h}<span style={u}>hr</span></>}{(m > 0 || !h) && <>{m}<span style={u}>min</span></>}</span>; };

const Room = ({ r, s, d }) => {
  const n = now(s), col = K.room(r.r) || K.fg, inn = r.st === 'in', soon = inn && r.till - n < 15 * 60e3;
  const tmr = r.next && sgd(r.next).getUTCDate() !== sgd(n).getUTCDate() && sgd(r.next).getUTCHours() >= 7;
  /* (25 Nov, owner) the status box follows the room's colour; "ending" fills it lightly instead of turning orange */
  const tint = (a) => col + Math.round(a * 255).toString(16).padStart(2, '0');
  const chip = inn ? <Chip c={col} strong fill={soon ? tint(0.16) : null}>{(soon ? 'Ending · ' : 'In · till ') + clk(r.till)}</Chip>
    : r.st === 'clean' ? <Chip c={col}>{'Cleaning till ' + clk(r.till)}</Chip>
    : r.st === 'next' ? <Chip c={col} dim>{'Next ' + clk(r.next) + (tmr ? ' tmr' : '')}</Chip> : <Chip>Free</Chip>;
  const who = inn ? r.n : r.st === 'next' ? r.nn : '';
  const tv = r.tv ? (r.tv.off ? 'TV off' : r.tv.quiet ? 'TV not reporting' : r.tv.app || 'TV on') : '';
  const tvBad = inn && r.tv && (r.tv.off || r.tv.quiet);
  const ps = r.ps4 && r.ps4.on ? r.ps4.g || 'PS4 menu' : '';
  const A = (r.msgs || []).filter(isAuto), S = (r.msgs || []).filter((m) => !isAuto(m));
  /* right-hand side: the ring (session used / cleaning / time to the next guest) and, under it, who's after this guest */
  const big = (t) => <span style={{ fontWeight: 300, fontSize: 19, lineHeight: 1, fontVariantNumeric: 'tabular-nums' }}>{t}</span>;
  /* (25 Nov, owner: "the circles … dont have uniform design") – the ring has ONE meaning: it only appears while guests are in,
     fills as their booking is used, and says how long they booked. Every other countdown is the big number on the left. */
  const ring = inn && r.since ? <Ring p={(n - r.since) / (r.till - r.since)} col={col}>{big(hrs(r.till - r.since))}<Kick size={6}>booked</Kick></Ring> : null;
  /* the big number on the left: always the time to this room's next change, with words saying what that change is */
  const cd = inn && r.till ? [r.till - n, 'left'] : r.st === 'clean' && r.till ? [r.till - n, 'cleaning left'] : r.st === 'next' && r.next > n && r.next - n < 3 * 3600e3 ? [r.next - n, 'till arrival'] : null;
  const then = (inn || r.st === 'clean') && r.next ? <div style={{ textAlign: 'right', display: 'grid', gap: 4, justifyItems: 'end' }}>
      <Kick size={6}>then</Kick>
      <span style={{ fontWeight: 600, whiteSpace: 'nowrap' }}>{clk(r.next) + (tmr ? ' tmr' : '')}</span>
      {NAMES && r.nn && <span style={{ color: K.sub, maxWidth: 110, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{r.nn}</span>}
      {r.gap != null && !tmr && <span style={{ fontSize: 11.5, color: r.gap < 30 ? K.warn : K.sub }}>{r.gap + ' min gap'}</span>}</div>
    : r.st === 'free' ? <div style={{ textAlign: 'right', color: K.sub, fontSize: 12 }}>{r.next ? 'next ' + clk(r.next) + (tmr ? ' tmr' : '') : 'nothing more tonight'}</div> : null;
  return (
    <Card color={col} lit={inn} title={'Double-click for ' + r.r + "'s details"} onDoubleClick={() => d && d({ type: 'OPEN', r: r.r })} style={{ display: 'flex', flexDirection: 'column' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}><Pix name={RICON[r.r]} color={col} size={15} />
        <span style={{ fontFamily: 'PS2P', fontSize: 12, letterSpacing: '.12em', color: col }}>{r.r.toUpperCase()}</span></div>
      <div style={{ display: 'flex', gap: 12, marginTop: 13, flex: 1 }}>
        <div style={{ flex: 1, minWidth: 0 }}>
          {chip}
          {cd && <div style={{ display: 'flex', alignItems: 'baseline', gap: 8, marginTop: 10 }}>
            <Dur ms={cd[0]} color={soon ? col : inn ? K.fg : K.sub} />
            <Kick>{cd[1]}</Kick></div>}
          <div style={{ display: 'grid', gap: 5, marginTop: 12, fontSize: 13 }}>
            {NAMES && who && <Line icon="user" bold>{who}</Line>}
            {inn && r.x ? <Line color={col}>Extended</Line> : null}
            {tv && (inn || (r.tv && !r.tv.off && !r.tv.quiet)) && <Line icon="tv" color={tvBad ? K.warn : undefined}>{tv}</Line>}
            {ps && <Line icon="pad">{ps}</Line>}
          </div>
        </div>
        {(ring || then) && <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-end', gap: 12, flex: 'none' }}>{ring}{then}</div>}
      </div>
      {(A.length > 0 || S.length > 0) && <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 12 }}>
        {A.slice(0, 2).map((m) => <Chip key={m.at + m.k} sm c={mC(m, n)}>{(m.k === 'wrapup' ? '10m ' : 'Wrap ') + clkFull(m.at).replace(/[ap]m$/, '') + (m.ok ? ' ✓' : mState(m, n) === 'wait' ? ' …' : '')}</Chip>)}
        {S.slice(0, 2).map((m) => <Chip key={m.at + m.ti} sm c={mState(m, n) === 'next' ? K.fg : mC(m, n)}>{clk(m.at) + ' ' + (m.ti || 'TV message').slice(0, 26) + (m.ok ? ' ✓' : '')}</Chip>)}
      </div>}
    </Card>);
};

/* ---------- the strip's tiles – pick up to 5 (25 Nov, owner: "give me several bottom tiles … and ill decide") ---------- */
const STRIP_MAX = 5;
const R1 = ({ r, children, c }) => <div style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 0, color: c }}><Pix name={RICON[r]} color={K.room(r)} size={11} />{children}</div>;
const Txt = ({ children, sub, bold, c, keep }) => <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', minWidth: 0, flex: keep ? 'none' : undefined, color: c || (sub ? K.sub : undefined), fontWeight: bold ? 600 : 500 }}>{children}</span>;
const None = ({ children }) => <div style={{ color: K.sub }}>{children}</div>;
const sameNight = (a, n) => { const k = (t) => { const x = sgd(t - 7 * 3600e3); return x.getUTCFullYear() * 400 + x.getUTCMonth() * 31 + x.getUTCDate(); }; return k(a) === k(n); };   /* the shop night runs to 7am */
const TILES = {
  next: { name: 'Up next', what: 'the next guests in and out, with how long to go', title: () => 'Up next', w: '1.15fr',
    body: ({ G, n }) => G.length ? G.slice(0, 4).map((g, i) => <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 7, opacity: i ? 0.75 : 1 }}>
      {g.rs.slice(0, 3).map((r) => <Pix key={r} name={RICON[r]} color={K.room(r)} size={11} />)}
      <Txt bold>{(g.rs.length > 2 ? g.rs.length + ' rooms' : g.rs.join(' · ')) + (g.t === 'out' ? ' out ' : ' in ') + clk(g.at)}</Txt>
      <span style={{ flex: 1 }} /><span style={{ fontWeight: 600, whiteSpace: 'nowrap', color: i === 0 && g.at - n < 15 * 60e3 ? K.warn : i === 0 ? K.fg : K.sub }}>{'in ' + durTxt(g.at - n)}</span></div>)
      : <None>Nothing more tonight</None> },
  today: { name: 'Today', what: 'bookings made today, paid / not paid, cancelled, the latest one', title: () => 'Today',
    body: ({ M, n }) => M ? <>
      <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}><span style={{ fontWeight: 300, fontSize: 28, lineHeight: 1 }}>{M.n}</span><span style={{ color: K.sub }}>booked</span>
        <span style={{ flex: 1 }} /><span style={{ fontWeight: 600, fontSize: 16 }}>{money(M.paid)}</span><span style={{ color: K.sub }}>paid</span></div>
      {(M.nUnpaid > 0 || M.cx > 0) && <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
        {M.nUnpaid > 0 && <Chip sm c={K.warn}>{M.nUnpaid + ' not paid · ' + money(M.unpaid)}</Chip>}
        {M.cx > 0 && <Chip sm c={K.bad}>{M.cx + ' cancelled'}</Chip>}</div>}
      {M.last && <R1 r={M.last.r} c={K.sub}><Txt sub>{'Latest: ' + M.last.r + (NAMES && M.last.n ? ' · ' + M.last.n : '') + ' · ' + ago(M.last.t, n)}</Txt></R1>}
    </> : <None>–</None> },
  tomorrow: { name: 'Tomorrow', what: "tomorrow's bookings in time order", title: ({ T }) => 'Tomorrow' + (T && T.first ? ' · ' + dayName(T.first.at) : ''), w: '1.2fr',
    body: ({ T }) => !T || !T.n ? <None>Nothing booked yet</None> : <>{(T.list || [T.first]).slice(0, 5).map((x, i) => <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 0 }}>
      <span style={{ width: 54, flex: 'none', fontWeight: 600, fontVariantNumeric: 'tabular-nums' }}>{clk(x.at)}</span><Pix name={RICON[x.r]} color={K.room(x.r)} size={11} />
      <span style={{ fontWeight: 600 }}>{x.r}</span><Txt sub>{NAMES && x.n ? x.n : x.e ? 'till ' + clk(x.e) : ''}</Txt></div>)}
      {T.n > 5 && <None>{'+ ' + (T.n - 5) + ' more'}</None>}</> },
  health: { name: 'Health', what: 'anything that needs someone, SimplyBook use, when the board last updated', title: () => 'Health',
    body: ({ H, s, j, stale }) => <>
      {H.list && H.list.length ? <div style={{ display: 'grid', gap: 6, justifyItems: 'start', minWidth: 0 }}>{H.list.slice(0, 3).map((x, i) => <Chip key={i} sm c={x.lv >= 2 ? K.bad : K.warn}>{x.t.replace(/^[^\w"]+/, '').slice(0, 34)}</Chip>)}</div>
        : <Chip sm c={K.ok}>All systems OK</Chip>}
      {H.sb != null && <div style={{ color: K.sub, fontSize: 11.5 }}>{H.sb + ' SimplyBook requests today'}</div>}
      <div style={{ color: stale || s.err ? K.warn : K.sub, fontSize: 11 }}>{s.err ? s.err : 'Updated ' + ago(s.at, Date.now()) + (j.age > 15 ? ' · engine copy ' + j.age + ' min old' : '')}</div></> },
  free: { name: 'Free now', what: 'which rooms you can give a walk-in right now, and until when', title: () => 'Free now',
    body: ({ rooms, n }) => { const L = rooms.filter((r) => r.st === 'free' || r.st === 'next' || r.st === 'clean');
      return L.length ? L.slice(0, 6).map((r) => { const from = r.st === 'clean' ? r.till : n, till = r.next && sameNight(r.next, n) ? r.next : 0;
        return <R1 key={r.r} r={r.r}><Txt bold keep>{r.r}</Txt><span style={{ flex: 1 }} />
          <Txt sub c={till && till - from < 90 * 60e3 ? K.warn : undefined}>{(r.st === 'clean' ? clk(r.till) + '–' : till ? 'till ' : 'all night') + (till ? clk(till) + ' · ' + durTxt(till - from) : r.st === 'clean' ? 'late' : '')}</Txt></R1>; })
        : <None>Every room is in use</None>; } },
  msgs: { name: 'TV messages', what: "every room's coming TV messages – orange = on a TV waiting for the guest's OK", title: () => 'TV messages', w: '1.2fr',
    body: ({ rooms, n }) => { const L = []; rooms.forEach((r) => (r.msgs || []).forEach((m) => L.push({ ...m, r: r.r })));
      L.sort((a, b) => (mState(a, n) === 'wait' ? 0 : 1) - (mState(b, n) === 'wait' ? 0 : 1) || a.at - b.at);
      const Lv = L.filter((m) => !m.ok || n - m.ok < 10 * 60e3);
      return Lv.length ? Lv.slice(0, 5).map((m, i) => <R1 key={i} r={m.r}><Dot c={mC(m, n)} size={5} /><span style={{ width: 52, flex: 'none', fontWeight: 600 }}>{clk(m.at)}</span>
        <Txt sub={mState(m, n) === 'next'}>{m.k === 'wrapup' ? '10-min warning' : m.k === 'timeup' ? "That's a wrap" : (m.ti || 'TV message')}</Txt></R1>)
        : <None>None coming up</None>; } },
  screens: { name: 'TVs & PS4s', what: "what each room's TV and PS4 are showing, and any TV that's off or not reporting", title: () => 'TVs & PS4s', w: '1.2fr',
    body: ({ rooms }) => rooms.filter((r) => r.st !== 'closed').map((r) => { const bad = r.tv && (r.tv.off || r.tv.quiet), inn = r.st === 'in';
      const tv = !r.tv ? '–' : r.tv.off ? 'TV off' : r.tv.quiet ? 'not reporting' : (r.tv.app || 'TV on');
      return <R1 key={r.r} r={r.r}><span style={{ width: 62, flex: 'none', fontWeight: 600 }}>{r.r}</span><Txt sub={!inn} c={inn && bad ? K.warn : undefined}>{tv + (r.ps4 && r.ps4.on ? ' · ' + (r.ps4.g || 'PS4 menu') : '')}</Txt></R1>; }) },
  turn: { name: 'Turnovers', what: 'the gaps between guests tonight – how long you have to clean each room (orange under 30 min)', title: () => 'Turnovers',
    body: ({ rooms, n }) => { const L = rooms.filter((r) => (r.st === 'in' || r.st === 'clean') && r.next && r.gap != null && sameNight(r.next, n)).sort((a, b) => a.gap - b.gap);
      return L.length ? L.slice(0, 5).map((r) => <R1 key={r.r} r={r.r}><Txt bold keep>{r.r}</Txt><Txt sub>{clk(r.till) + ' → ' + clk(r.next)}</Txt><span style={{ flex: 1 }} />
        <span style={{ fontWeight: 600, whiteSpace: 'nowrap', color: r.gap < 30 ? K.warn : K.fg }}>{durTxt(r.gap * 60e3)}</span></R1>)
        : <None>No back-to-back guests</None>; } },
  night: { name: 'The night', what: 'guests tonight (done / in / to come), when the last guest leaves, first guest tomorrow', title: () => 'The night',
    body: ({ c, j, n, T }) => { const outs = (j.events || []).filter((e) => e.t === 'out' && sameNight(e.at, n)), last = outs.length ? outs[outs.length - 1] : null;
      return <>
        <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}><span style={{ fontWeight: 300, fontSize: 28, lineHeight: 1 }}>{c.total || 0}</span><span style={{ color: K.sub }}>guests tonight</span></div>
        <div style={{ color: K.sub }}>{(c.done || 0) + ' done · ' + (c.in || 0) + ' in · ' + (c.left || 0) + ' to come'}</div>
        {last ? <R1 r={last.r}><Txt>{'Last out ' + clk(last.at) + ' · ' + last.r}</Txt></R1> : <None>Nobody left to leave</None>}
        {T && T.first && <R1 r={T.first.r}><Txt sub>{'First in tomorrow ' + clk(T.first.at)}</Txt></R1>}</>; } },
};
const STRIP_ALL = ['next', 'today', 'tomorrow', 'health', 'free', 'msgs', 'screens', 'turn', 'night'];
/* choosing: tap tiles in the order you want them (up to 5); tap again to take one off */
const Picker = ({ s, d }) => { const cur = s.strip || STRIP;
  return <div style={{ display: 'grid', gap: 12 }}>
    <div style={{ color: K.sub }}>{'Hold your Übersicht key and tap the tiles you want, in order – up to ' + STRIP_MAX + '. Tap again to remove. Then Done.'}</div>
    <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 10 }}>{STRIP_ALL.map((k) => { const i = cur.indexOf(k), on = i >= 0;
      return <div key={k} role="button" onClick={() => d({ type: 'STRIP', list: on ? cur.filter((x) => x !== k) : cur.length < STRIP_MAX ? [...cur, k] : cur })}
        style={{ display: 'flex', gap: 10, alignItems: 'flex-start', padding: '10px 12px', borderRadius: 8, border: `1px solid ${on ? K.fg : K.line}`, cursor: 'pointer', opacity: on || cur.length < STRIP_MAX ? 1 : 0.45 }}>
        <span style={{ flex: 'none', width: 18, height: 18, borderRadius: 5, border: `1px solid ${on ? K.fg : K.line2}`, display: 'grid', placeItems: 'center', fontFamily: 'PS2P', fontSize: 7, background: on ? K.fg : 'transparent', color: K.bg }}>{on ? i + 1 : ''}</span>
        <div style={{ display: 'grid', gap: 3, minWidth: 0 }}><b style={{ fontWeight: 600 }}>{TILES[k].name}</b><span style={{ color: K.sub, fontSize: 11.5, lineHeight: 1.35 }}>{TILES[k].what}</span></div></div>; })}</div>
  </div>; };

/* ---------- a room's details (double-click a room) – over the room tiles, in the room's colour ---------- */
const mWords = (m, n) => m.ok ? "OK'd on the TV " + clk(m.ok) : m.ak === 0 ? (m.at > n ? 'shows ' + clk(m.at) + ' · no OK needed' : 'shown · no OK needed') : m.at > n ? 'shows ' + clk(m.at) : 'on the TV – waiting for OK';
const mName = (m) => m.k === 'wrapup' ? '10-minute warning' : m.k === 'timeup' ? "That's a wrap" : (m.ti || 'TV message');
const Detail = ({ r, j, s, d }) => {
  const n = now(s), col = K.room(r.r) || K.fg, inn = r.st === 'in', T = j.tomorrow || {};
  const ev = (j.events || []).filter((e) => e.r === r.r), tm = (T.list || []).filter((x) => x.r === r.r);
  const tv = r.tv ? (r.tv.off ? 'TV off' : r.tv.quiet ? 'TV not reporting' : (r.tv.app || 'TV on') + (r.tv.at ? ' · since ' + clk(r.tv.at) : '')) : 'TV not reporting yet';
  const cd = inn ? [r.till - n, 'left'] : r.st === 'clean' ? [r.till - n, 'cleaning left'] : r.st === 'next' && r.next > n ? [r.next - n, 'till arrival'] : null;
  const head = inn ? 'In · till ' + clk(r.till) : r.st === 'clean' ? 'Cleaning till ' + clk(r.till) : r.st === 'next' ? 'Next ' + clk(r.next) : 'Free';
  const Row = ({ a, b, c }) => <div style={{ display: 'flex', gap: 10, alignItems: 'baseline', minWidth: 0 }}><span style={{ width: 62, flex: 'none', fontWeight: 600, fontVariantNumeric: 'tabular-nums' }}>{a}</span>
    <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', minWidth: 0 }}>{b}</span>{c && <span style={{ color: K.sub, whiteSpace: 'nowrap' }}>{c}</span>}</div>;
  return <div onDoubleClick={() => d({ type: 'OPEN', r: '' })} style={{ position: 'absolute', inset: 0, background: K.bg, border: `1px solid ${col}`, borderRadius: 12, padding: '18px 20px', display: 'flex', flexDirection: 'column', gap: 16, overflow: 'hidden' }}>
    <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
      <Pix name={RICON[r.r]} color={col} size={18} /><span style={{ fontFamily: 'PS2P', fontSize: 15, letterSpacing: '.12em', color: col }}>{r.r.toUpperCase()}</span>
      <Chip c={col} strong={inn}>{head}</Chip><span style={{ flex: 1 }} />
      <Btn onClick={() => { try { run('open "https://staff.round2.sg/"'); } catch (e) {} }}>Open control centre</Btn>
      <Btn onClick={() => d({ type: 'OPEN', r: '' })}>✕ Close</Btn>
    </div>
    <div style={{ display: 'grid', gridTemplateColumns: '1.1fr 1fr 1fr', gap: 0, flex: 1, minHeight: 0 }}>
      <div style={{ display: 'grid', gap: 10, alignContent: 'start', paddingRight: 20 }}>
        <Kick>Now</Kick>
        <div style={{ display: 'flex', gap: 16, alignItems: 'center' }}>
          {cd ? <div style={{ display: 'grid', gap: 6 }}><Dur ms={cd[0]} size={40} color={inn ? K.fg : K.sub} /><Kick>{cd[1]}</Kick></div>
            : <span style={{ color: K.sub }}>{r.next ? 'Next guest ' + clk(r.next) : 'Nothing more tonight'}</span>}
          <span style={{ flex: 1 }} />
          {inn && r.since && <Ring p={(n - r.since) / (r.till - r.since)} col={col} size={84}><span style={{ fontWeight: 300, fontSize: 18 }}>{hrs(r.till - r.since)}</span><Kick size={6}>booked</Kick></Ring>}
        </div>
        <div style={{ display: 'grid', gap: 6, fontSize: 13 }}>
          {NAMES && (inn ? r.n : r.nn) && <Line icon="user" bold>{inn ? r.n : r.nn}</Line>}
          {inn && r.since && <Line icon="clock">{'Booked ' + clk(r.since) + ' – ' + clk(r.till)}</Line>}
          {inn && r.x ? <Line color={col}>Extended</Line> : null}
          <Line icon="tv" color={inn && r.tv && (r.tv.off || r.tv.quiet) ? K.warn : undefined}>{tv}</Line>
          <Line icon="pad">{r.ps4 && r.ps4.on ? (r.ps4.g || 'PS4 menu') : 'PS4 off or quiet'}</Line>
          {r.next && (inn || r.st === 'clean') && <Line icon="bell">{'Then ' + clk(r.next) + (NAMES && r.nn ? ' · ' + r.nn : '') + (r.gap != null ? ' · ' + r.gap + ' min gap' : '')}</Line>}
        </div>
      </div>
      <div style={{ display: 'grid', gap: 10, alignContent: 'start', padding: '0 20px', borderLeft: `1px solid ${K.line}`, minWidth: 0 }}>
        <Kick>TV messages</Kick>
        {(r.msgs || []).length ? r.msgs.map((m) => <div key={m.at + (m.k || '') + (m.ti || '')} style={{ display: 'grid', gap: 3 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 0 }}><Dot c={mC(m, n)} /><b style={{ fontWeight: 600, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{clk(m.at) + ' · ' + mName(m)}</b></div>
          <span style={{ color: K.sub, fontSize: 12, paddingLeft: 14 }}>{(isAuto(m) ? 'automatic · ' : 'scheduled' + (m.fr ? ' by ' + m.fr : '') + ' · ') + mWords(m, n)}</span>
          {m.tx && <div style={{ marginLeft: 14, marginTop: 3, padding: '8px 10px', borderLeft: `1.5px solid ${col}`, color: K.fg, fontSize: 12.5, lineHeight: 1.45, whiteSpace: 'pre-wrap', wordBreak: 'break-word', maxHeight: 120, overflow: 'hidden' }}>{m.tx}</div>}</div>)
          : <span style={{ color: K.sub }}>None from 15 min ago onwards</span>}
      </div>
      <div style={{ display: 'grid', gap: 9, alignContent: 'start', paddingLeft: 20, borderLeft: `1px solid ${K.line}`, minWidth: 0 }}>
        <Kick>{'Tonight · ' + r.r}</Kick>
        {ev.length ? ev.slice(0, 6).map((e, i) => <Row key={i} a={clk(e.at)} b={e.t === 'in' ? 'in' : 'out'} c={NAMES && e.n ? e.n : ''} />) : <span style={{ color: K.sub }}>Nothing more tonight</span>}
        <div style={{ height: 4 }} />
        <Kick>Tomorrow</Kick>
        {tm.length ? tm.slice(0, 4).map((x, i) => <Row key={i} a={clk(x.at)} b={x.e ? 'till ' + clk(x.e) : ''} c={NAMES && x.n ? x.n : ''} />) : <span style={{ color: K.sub }}>Nothing booked yet</span>}
      </div>
    </div>
  </div>;
};

export const render = (s, d) => {
  escD = d;
  K = themeOf(lightNow(s.mode));
  const j = s.j, n = now(s);
  const fonts = <style>{`
    @font-face { font-family: PS2P; src: url(${FONTS}PressStart2P-Regular.ttf); }
    @font-face { font-family: QS; font-weight: 300 700; src: url(${FONTS}Quicksand.ttf); }`}</style>;
  const at = s.pos ? { top: s.pos.y, left: s.pos.x, transformOrigin: 'top left' } : { top: TOP, [SIDE]: EDGE, transformOrigin: `top ${SIDE}` };
  const wrap = (body) => <div data-board="1" style={{ position: 'fixed', ...at, width: 1100, transform: `scale(${s.scale || SCALE})`, fontFamily: 'QS, -apple-system, sans-serif', fontSize: 13, color: K.fg,
    background: K.bg, borderRadius: 18, padding: '22px 24px 24px', border: `1px solid ${K.line}`, userSelect: 'none', WebkitUserSelect: 'none' }}>{fonts}{body}
    {/* the resize mark: a thin corner like the logo's brackets – drag to resize, double-click to put the board back */}
    <div title="Drag to resize · double-click to put it back" onMouseDown={(e) => d && startDrag(e, s, d, 'size')} onDoubleClick={() => d && d({ type: 'RESET' })}
      style={{ position: 'absolute', right: 7, bottom: 7, width: 14, height: 14, borderRight: `1.5px solid ${K.line2}`, borderBottom: `1.5px solid ${K.line2}`, borderBottomRightRadius: 5, cursor: 'nwse-resize' }} />
  </div>;
  const logo = (h) => <img src={LOGO} style={{ height: h, display: 'block', filter: K.logo }} />;
  if (!j) return wrap(<div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>{logo(22)}<span style={{ color: K.sub }}>{s.err || 'Loading the board…'}</span></div>);

  const c = j.counts || {}, H = j.health || { lv: 0, list: [] }, hc = [K.ok, K.warn, K.bad][H.lv] || K.ok, M = j.money, T = j.tomorrow, rooms = j.rooms || [];
  const G = []; for (const e of (j.events || []).filter((e) => e.at > n)) { const g = G[G.length - 1]; if (g && g.t === e.t && Math.abs(g.at - e.at) < 60e3) g.rs.push(e.r); else G.push({ at: e.at, t: e.t, rs: [e.r] }); }
  const stale = Date.now() - s.at > 2 * 60e3;
  const X = { j, n, s, c, H, M, T, rooms, G, stale };
  return wrap(<div>
    <div title="Drag to move" onMouseDown={(e) => d && startDrag(e, s, d, 'move')} style={{ display: 'flex', alignItems: 'center', gap: 22, cursor: drag ? 'grabbing' : 'grab' }}>
      {logo(26)}
      <div><Kick>Tonight</Kick><div style={{ marginTop: 7, color: K.sub, fontWeight: 500, fontSize: 14 }}>{T && T.over ? 'All done – tomorrow is on the right' : <><b style={{ color: K.fg, fontWeight: 700 }}>{c.in}</b> in · <b style={{ color: K.fg, fontWeight: 700 }}>{c.left}</b> to come · {c.done} done</>}</div></div>
      <div style={{ flex: 1 }} />
      {/* light / dark switch (25 Nov): tap to go dark → light → auto (follows the Mac) – remembered */}
      {d && <span role="button" title="Dark / light / auto" onClick={(e) => { e.stopPropagation(); d({ type: 'MODE' }); }} onMouseDown={(e) => e.stopPropagation()}
        style={{ display: 'inline-flex', alignItems: 'center', gap: 7, padding: '6px 10px', borderRadius: 6, border: `1px solid ${K.line2}`, cursor: 'pointer' }}>
        <svg width="12" height="12" viewBox="0 0 12 12" style={{ display: 'block' }}><circle cx="6" cy="6" r="5.25" fill="none" stroke={K.fg} strokeWidth="1.2" /><path d="M6 .75a5.25 5.25 0 0 1 0 10.5z" fill={K.fg} /></svg>
        <Kick color={K.fg}>{s.mode || MODE}</Kick></span>}
      <Chip c={hc} strong={H.lv >= 1}>{H.lv >= 2 ? 'Needs someone' : H.lv ? 'Keep an eye' : 'All systems OK'}</Chip>
      <div style={{ textAlign: 'right' }}><Kick>{dayName(n)}</Kick><div style={{ fontWeight: 300, fontSize: 26, lineHeight: 1, marginTop: 6, fontVariantNumeric: 'tabular-nums' }}>{clkFull(n)}</div></div>
    </div>
    <div style={{ height: 1, margin: '18px 0 20px', background: K.line }} />
    {/* the six rooms are the board: full width, two even rows */}
    <div style={{ position: 'relative' }}>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gridAutoRows: 'minmax(196px, auto)', gap: 14 }}>{rooms.filter((r) => r.st !== 'closed').map((r) => <Room key={r.r} r={r} s={s} d={d} />)}</div>
      {s.open && rooms.find((r) => r.r === s.open) && <Detail r={rooms.find((r) => r.r === s.open)} j={j} s={s} d={d} />}
    </div>
    {rooms.filter((r) => r.st === 'closed').map((r) => <div key={r.r} style={{ display: 'flex', alignItems: 'center', gap: 9, opacity: 0.6, padding: '12px 0 0 4px' }}>
      <Pix name={RICON[r.r]} color={K.room(r.r)} size={11} /><span style={{ fontFamily: 'PS2P', fontSize: 9, letterSpacing: '.12em', color: K.room(r.r) }}>{r.r.toUpperCase()}</span><span style={{ color: K.sub }}>closed – renovation</span></div>)}
    {/* everything else sits quietly underneath: the strip – columns split by thin lines, no boxes. Which columns, and in
        which order, is the owner's choice (25 Nov): "Choose" at the right of the line, or STRIP at the top of this file */}
    <div style={{ position: 'relative', margin: '18px 0 16px' }}>
      <div style={{ height: 1, background: K.line }} />
      {d && <span role="button" onClick={() => d({ type: 'PICK' })} style={{ position: 'absolute', right: 0, top: -4, padding: '0 0 0 10px', background: K.bg, color: s.pick ? K.fg : K.sub, cursor: 'pointer' }}><Kick color={s.pick ? K.fg : K.sub}>{s.pick ? 'Done' : 'Choose'}</Kick></span>}
    </div>
    {s.pick ? <Picker s={s} d={d} /> :
    <div style={{ display: 'grid', gridTemplateColumns: (s.strip || STRIP).map((k) => (TILES[k] || {}).w || '1fr').join(' '), alignItems: 'start' }}>
      {(s.strip || STRIP).filter((k) => TILES[k]).map((k, i) => <Col key={k} first={i === 0} title={TILES[k].title(X)}>{TILES[k].body(X)}</Col>)}
    </div>}
  </div>);
};

export const className = ``;   /* clicks only reach the board while Übersicht's interaction key is held */
