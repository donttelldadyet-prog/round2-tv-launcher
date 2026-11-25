// Round2 board · VAPORWAVE – a desktop widget for the Mac (Übersicht). Owner, 25 Nov 2026: "maybe try this different react UI
// base" (webuiprompt.com/design/vaporwave – Vaporwave / Outrun design system): the void, a perspective grid floor, a blurred
// sunset sun, CRT scanlines, terminal window chrome with > prompts, skewed neon badges, Orbitron + Share Tech Mono.
// Room colours stay each room's identity (top laser line, name glow, icon); the Round2 logo stays white and untouched.
// Get it from the control centre (Phone alerts & widgets › Download Mac board – Vaporwave); put it in Übersicht's widgets folder.
// Same live data as the other boards (the engine's /w), checked every 30 s; countdowns tick every second on the engine's clock.
//
// Change these to taste:
const SIDE = 'left';          // 'left' or 'right' – macOS keeps desktop icons on the right
const TOP = 46;               // points from the top (the MacBook's menu bar + notch are ~37)
const EDGE = 24;              // points from the side
const SCALE = 1;              // 1 = made for the 16" MacBook Pro's default size (1728 × 1117); 0.85 smaller, 1.15 bigger
const NAMES = true;           // false = hide guest names
const MOTION = false;         // true = the grid floor scrolls (pretty, but keeps the Mac's graphics busy all day)

const KEY = '__KEY__', API = '__URL__';
const FONTS = 'https://staff.round2.sg/widget/fonts/';

import { React } from 'uebersicht';

const RC = { Vale: '#E8436A', Kaeden: '#4DB848', Scarlett: '#F2C21B', Bella: '#7A5BE0', Kai: '#24B49A', Atlas: '#E9E7EF', Max: '#E0402F' };
const INK = '#f4f1fb', MUTE = '#8d84a6', ACC = '#F2C21B', OK = '#4DB848', WARN = '#ff9f43', BAD = '#ff4d5e';
const RICON = { Vale: 'cat', Kaeden: 'leaf', Scarlett: 'saturn', Kai: 'mount', Bella: 'book', Atlas: 'boat', Max: 'car' };
const PIX = {"tv":["..#.....#...","...#...#....","....#.#.....","############","#..........#","#...#......#","#...###....#","#...#####..#","#...###....#","#...#......#","#..........#","############"],"clock":["...######...","..#......#..",".#...#....#.","#....#.....#","#....#.....#","#....###...#","#.......#..#","#........#.#","#..........#",".#........#.","..#......#..","...######..."],"user":["....####....","...#....#...","...#....#...","...#....#...","....####....","............","..########..",".#........#.","#..........#","#..........#","#..........#","############"],"cake":[".....##.....","......#.....",".....##.....",".....##.....",".##########.",".#........#.",".#.##..##.#.",".##########.","#..........#","#.#.#..#.#.#","#..........#","############"],"cat":["............",".#.......#..",".##.....##..",".#.#####.#..",".#.......#..","#..#...#..#.","#.........#.","#....#....#.",".#..#.#..#..","..#.....#...","...#####....","............"],"mount":["............","............",".....#......","....#.#.....","...#...#.#..","..#.#.#.#.#.","..#..#..#..#",".#.........#",".#.........#","#...........","############","............"],"book":["............",".####..####.","#....##....#","#.##.##.##.#","#....##....#","#.##.##.##.#","#....##....#","#.##.##.##.#","#....##....#",".####..####.",".....##.....","............"],"boat":[".....#......",".....##.....",".....###....",".....####...",".....#####..",".....######.",".....#......","############",".##########.","..########..","............","#.##.##.##.#"],"car":["............","............","...######...","..#..#...#..",".#...#....#.","############","#..........#","#.##....##.#","############",".##......##.",".##......##.","............"],"saturn":["..............","..............",".....####.....","....######..##","...#########.#","...#######.##.","...#####..#...","...##...###...",".##..######...","#.#########...","##..######....",".....####.....","..............",".............."],"leaf":[".........###","......######","....#####..#","...####...#.","..####...#..","..###...#...",".###...#....",".##...#.....",".#.###......",".###........","#...........","............"],"moon":["....####....","..###.......",".##.........",".#..........","##..........","##..........","##..........","##.........#",".##.......##",".###.....##.","..########..","....####...."],"pad":["............","..########..",".#........#.","#..#....#..#","#.###..#.#.#","#..#....#..#","#..........#","#...####...#",".#.#....#.#.","..#......#..","............","............"],"bell":[".....##.....","....####....","...######...","...######...","..########..","..########..",".##########.","############","............",".....##.....","............","............"],"warn":[".....##.....","....####....","....#..#....","...##..##...","...##..##...","..###..###..","..###..###..",".##########.",".####..####.","#####..#####","############","............"],"check":["............","...........#","..........##",".........##.","#.......##..","##.....##...",".##...##....","..##.##.....","...###......","....#.......","............","............"],"sparkle":["......#.....","......#.....",".....###....","###########.",".....###....","......#.....","..#...#.....","..#.........",".###......#.","..#......###","..#.......#.","............"]};

const LOGO = 'data:image/png;base64,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';

/* ---------- data: every 30 s; the screen ticks every second ---------- */
export const refreshFrequency = 1000;
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
export const initialState = { j: null, off: 0, err: '', at: 0, tick: 0 };
export const updateState = (ev, s) => {
  if (ev.type === 'DATA') return { ...s, j: ev.j, off: ev.off, err: '', at: Date.now() };
  if (ev.type === 'ERR') return { ...s, err: ev.e };
  if (ev.type === 'TICK') return { ...s, tick: s.tick + 1 };
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

/* ---------- vaporwave tokens ---------- */
const VOID = '#090014', CHROME = '#E0E0E0', GLASS = 'rgba(26,16,60,0.8)', MAG = '#FF00FF', CYAN = '#00FFFF', SUN = '#FF9900', LINE = '#2D1B4E';
const DIM = 'rgba(224,224,224,0.55)';

const Pix = ({ name, color, size = 14, glow }) => {
  const P = PIX[name]; if (!P) return null;
  const rects = []; P.forEach((row, y) => { for (let x = 0; x < row.length; x++) if (row[x] === '#') rects.push(<rect key={x + '-' + y} x={x} y={y} width="1.02" height="1.02" />); });
  return <svg width={size} height={size * P.length / P[0].length} viewBox={`0 0 ${P[0].length} ${P.length}`} fill={color} style={{ display: 'block', flex: 'none', shapeRendering: 'crispEdges', filter: glow ? `drop-shadow(0 0 4px ${color})` : 'none' }}>{rects}</svg>;
};
/* a skewed neon tag (the design's badge / button shape); tone: cyan · mag · sun · dim · ok */
const TONE = { cyan: CYAN, mag: MAG, sun: SUN, dim: 'rgba(224,224,224,0.45)', ok: CYAN };
const Tag = ({ tone = 'dim', fill, pulse, children, sm }) => {
  const c = TONE[tone];
  return <span className={pulse ? 'vw-pulse' : ''} style={{ display: 'inline-block', transform: 'skewX(-12deg)', border: `${sm ? 1.5 : 2}px solid ${c}`, background: fill ? c : 'rgba(9,0,20,0.6)',
    boxShadow: tone === 'dim' ? 'none' : `0 0 ${sm ? 6 : 10}px ${c}`, padding: sm ? '2px 8px' : '4px 11px', maxWidth: '100%', whiteSpace: 'nowrap', overflow: 'hidden' }}>
    <span style={{ display: 'inline-block', transform: 'skewX(12deg)', fontFamily: 'STM', fontSize: sm ? 11.5 : 13, letterSpacing: '.12em', textTransform: 'uppercase', color: fill ? '#000' : c, textOverflow: 'ellipsis', overflow: 'hidden', maxWidth: '100%', verticalAlign: 'bottom' }}>{children}</span></span>;
};
/* terminal window: title bar with the three dots, > title */
const Win = ({ title, accent = CYAN, children, style }) => (
  <div style={{ border: `2px solid ${accent}`, background: 'rgba(0,0,0,0.72)', boxShadow: `0 0 20px ${accent === CYAN ? 'rgba(0,255,255,0.2)' : 'rgba(255,0,255,0.25)'}`, ...style }}>
    <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '6px 10px', background: accent === CYAN ? 'rgba(0,255,255,0.1)' : 'rgba(255,0,255,0.12)', borderBottom: `1px solid ${accent}` }}>
      <span style={{ display: 'flex', gap: 5 }}>{[MAG, CYAN, SUN].map((c) => <i key={c} style={{ width: 9, height: 9, borderRadius: '50%', background: c, display: 'block' }} />)}</span>
      <span style={{ fontFamily: 'STM', fontSize: 12, letterSpacing: '.18em', textTransform: 'uppercase', color: accent }}>{'> ' + title}</span></div>
    <div style={{ padding: '10px 12px', display: 'grid', gap: 8 }}>{children}</div></div>);
const Prompt = ({ children, color = CHROME, dim }) => <div style={{ display: 'flex', gap: 7, minWidth: 0, color: dim ? DIM : color }}><span style={{ color: MAG }}>{'>'}</span>
  <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', minWidth: 0 }}>{children}</span></div>;

const isAuto = (m) => m.k === 'wrapup' || m.k === 'timeup';
/* message tone: coming = dim · on the TV waiting for OK = magenta, pulsing · OK'd = cyan ✓ · shown, no OK needed = dim */
const mTone = (m, n) => m.ok ? 'cyan' : m.at > n || m.ak === 0 ? 'dim' : 'mag';

const Room = ({ r, s }) => {
  const n = now(s), col = RC[r.r] || CHROME, inn = r.st === 'in', soon = inn && r.till - n < 15 * 60e3;
  const tmr = r.st === 'next' && sgd(r.next).getUTCDate() !== sgd(n).getUTCDate();
  const tag = inn ? <Tag tone={soon ? 'sun' : 'cyan'} fill={soon}>{(soon ? 'ending ' : 'in · till ') + clk(r.till)}</Tag>
    : r.st === 'clean' ? <Tag tone="mag">{'cleaning · ' + clk(r.till)}</Tag>
    : r.st === 'next' ? <Tag tone="dim">{'next ' + clk(r.next) + (tmr ? ' tmr' : '')}</Tag> : <Tag tone="dim">free</Tag>;
  const who = inn ? r.n : r.st === 'next' ? r.nn : '';
  const tv = r.tv ? (r.tv.off ? 'TV off' : r.tv.quiet ? 'TV not reporting' : r.tv.app || 'TV on') : '';
  const tvBad = inn && r.tv && (r.tv.off || r.tv.quiet);
  const ps = r.ps4 && r.ps4.on ? r.ps4.g || 'PS4 menu' : '';
  const A = (r.msgs || []).filter(isAuto), S = (r.msgs || []).filter((m) => !isAuto(m));
  return (
    <div className="vw-card" style={{ position: 'relative', background: GLASS, WebkitBackdropFilter: 'blur(12px)', backdropFilter: 'blur(12px)', border: `1px solid ${mix(col, 0.35)}`, borderTop: `2px solid ${col}`,
      boxShadow: inn ? `0 0 26px ${mix(col, 0.35)}, inset 0 0 30px ${mix(col, 0.08)}` : `0 0 14px ${mix(col, 0.12)}`, padding: '12px 14px 14px' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 9 }}>
        <span style={{ width: 26, height: 26, transform: 'rotate(45deg)', border: `1.5px solid ${col}`, display: 'flex', alignItems: 'center', justifyContent: 'center', boxShadow: `0 0 8px ${mix(col, 0.6)}`, flex: 'none' }}>
          <span style={{ transform: 'rotate(-45deg)' }}><Pix name={RICON[r.r]} color={col} size={14} glow /></span></span>
        <span style={{ fontFamily: 'ORB', fontWeight: 900, fontSize: 17, letterSpacing: '.08em', color: col, textShadow: `0 0 8px ${mix(col, 0.8)}` }}>{r.r.toUpperCase()}</span></div>
      <div style={{ marginTop: 11 }}>{tag}</div>
      {inn && r.till && <div style={{ marginTop: 9, display: 'flex', alignItems: 'baseline', gap: 8 }}>
        <span style={{ fontFamily: 'ORB', fontWeight: 900, fontSize: 30, letterSpacing: '.02em', fontVariantNumeric: 'tabular-nums', backgroundImage: soon ? `linear-gradient(90deg, ${SUN}, ${MAG})` : `linear-gradient(90deg, ${SUN}, ${MAG}, ${CYAN})`, WebkitBackgroundClip: 'text', backgroundClip: 'text', color: 'transparent', filter: 'drop-shadow(0 0 10px rgba(255,0,255,0.45))' }}>{left(r.till - n)}</span>
        <span style={{ fontFamily: 'STM', fontSize: 11, letterSpacing: '.2em', color: DIM }}>LEFT</span></div>}
      <div style={{ display: 'grid', gap: 3, marginTop: 9, fontSize: 13.5 }}>
        {NAMES && who && <Prompt>{who}</Prompt>}
        {tv && (inn || (r.tv && !r.tv.off && !r.tv.quiet)) && <Prompt color={tvBad ? SUN : CHROME}>{tv}</Prompt>}
        {ps && <Prompt color={CYAN}>{'ps4: ' + ps}</Prompt>}
      </div>
      {(A.length > 0 || S.length > 0) && <div style={{ display: 'flex', flexWrap: 'wrap', gap: 7, marginTop: 10, paddingLeft: 3 }}>
        {A.slice(0, 2).map((m) => <Tag key={m.at + m.k} sm pulse={mTone(m, n) === 'mag'} tone={mTone(m, n)}>{(m.k === 'wrapup' ? '10m ' : 'wrap ') + clkFull(m.at).replace(/[ap]m$/, '') + (m.ok ? ' ✓' : '')}</Tag>)}
        {S.slice(0, 2).map((m) => <Tag key={m.at + m.ti} sm pulse={mTone(m, n) === 'mag'} tone={mTone(m, n) === 'dim' ? 'sun' : mTone(m, n)}>{clk(m.at) + ' ' + (m.ti || 'TV message').slice(0, 24) + (m.ok ? ' ✓' : '')}</Tag>)}
      </div>}
    </div>);
};

export const render = (s) => {
  const j = s.j, n = now(s);
  const css = <style>{`
    @font-face { font-family: ORB; font-weight: 400 900; src: url(${FONTS}Orbitron.ttf); }
    @font-face { font-family: STM; src: url(${FONTS}ShareTechMono-Regular.ttf); }
    @keyframes vw-pulse { 0%, 100% { opacity: 1 } 50% { opacity: .45 } }
    .vw-pulse { animation: vw-pulse 1.2s linear infinite; }
    @keyframes vw-grid { from { background-position: 0 0 } to { background-position: 0 40px } }
    @media (prefers-reduced-motion: reduce) { .vw-pulse, .vw-grid { animation: none } }`}</style>;
  const wrap = (body) => <div style={{ position: 'fixed', top: TOP, [SIDE]: EDGE, width: 1060, transform: `scale(${SCALE})`, transformOrigin: `top ${SIDE}`, fontFamily: 'STM, ui-monospace, monospace', fontSize: 13.5, color: CHROME,
    overflow: 'hidden', border: `2px solid ${CYAN}`, boxShadow: `0 0 50px rgba(0,255,255,0.2), 0 0 0 1px ${LINE}`, background: VOID }}>
    {css}
    {/* the floating sun, the grid floor, scanlines, a whisper of RGB shift */}
    <div style={{ position: 'absolute', width: 520, height: 520, right: -60, top: 120, borderRadius: '50%', background: `linear-gradient(180deg, ${SUN}, ${MAG})`, filter: 'blur(90px)', opacity: 0.22 }} />
    <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: 300, overflow: 'hidden', WebkitMaskImage: 'linear-gradient(to bottom, transparent, #000 70%)', maskImage: 'linear-gradient(to bottom, transparent, #000 70%)' }}>
      <div className="vw-grid" style={{ position: 'absolute', left: '-50%', right: '-50%', top: 0, height: 600, backgroundImage: `linear-gradient(transparent 95%, ${mix('#FF00FF', 0.55)} 95%), linear-gradient(90deg, transparent 95%, ${mix('#FF00FF', 0.55)} 95%)`, backgroundSize: '40px 40px', transform: 'perspective(500px) rotateX(60deg)', transformOrigin: 'top center', animation: MOTION ? 'vw-grid 2s linear infinite' : 'none' }} /></div>
    <div style={{ position: 'relative', zIndex: 1 }}>{body}</div>
    <div style={{ position: 'absolute', inset: 0, zIndex: 5, pointerEvents: 'none', background: 'linear-gradient(rgba(18,16,20,0) 50%, rgba(0,0,0,0.22) 50%)', backgroundSize: '100% 4px' }} />
    <div style={{ position: 'absolute', inset: 0, zIndex: 5, pointerEvents: 'none', background: 'linear-gradient(90deg, rgba(255,0,0,0.05), rgba(0,255,0,0.015), rgba(0,0,255,0.05))' }} />
  </div>;
  const bar = (title, right) => <div style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '8px 14px', background: 'rgba(0,255,255,0.1)', borderBottom: `1px solid ${CYAN}` }}>
    <span style={{ display: 'flex', gap: 6 }}>{[MAG, CYAN, SUN].map((c) => <i key={c} style={{ width: 11, height: 11, borderRadius: '50%', background: c, display: 'block', boxShadow: `0 0 6px ${c}` }} />)}</span>
    <span style={{ fontFamily: 'STM', letterSpacing: '.2em', color: CYAN, textTransform: 'uppercase', fontSize: 12.5 }}>{title}</span><span style={{ flex: 1 }} />{right}</div>;
  if (!j) return wrap(<div>{bar('round2://board', null)}<div style={{ padding: 20, display: 'flex', gap: 14, alignItems: 'center' }}><img src={LOGO} style={{ height: 22 }} /><Prompt dim>{s.err || 'booting the board…'}</Prompt></div></div>);

  const c = j.counts || {}, H = j.health || { lv: 0, list: [] }, M = j.money, T = j.tomorrow, rooms = j.rooms || [];
  const G = []; for (const e of (j.events || []).filter((e) => e.at > n)) { const g = G[G.length - 1]; if (g && g.t === e.t && Math.abs(g.at - e.at) < 60e3) g.rs.push(e.r); else G.push({ at: e.at, t: e.t, rs: [e.r] }); }
  const stale = Date.now() - s.at > 2 * 60e3;
  return wrap(<div>
    {bar('round2://tonight', <span style={{ fontFamily: 'STM', color: DIM, letterSpacing: '.12em' }}>{dayName(n).toUpperCase()}</span>)}
    <div style={{ padding: '16px 18px 20px' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 20, marginBottom: 16 }}>
        <img src={LOGO} style={{ height: 26, display: 'block' }} />
        <div style={{ fontFamily: 'ORB', fontWeight: 900, fontSize: 22, letterSpacing: '.06em', backgroundImage: `linear-gradient(90deg, ${SUN}, ${MAG}, ${CYAN})`, WebkitBackgroundClip: 'text', backgroundClip: 'text', color: 'transparent', filter: 'drop-shadow(0 0 14px rgba(255,0,255,0.5))', textTransform: 'uppercase' }}>
          {T && T.over ? 'Tonight done' : c.in + ' in · ' + c.left + ' to come'}</div>
        <span style={{ flex: 1 }} />
        <Tag tone={H.lv >= 2 ? 'mag' : H.lv ? 'sun' : 'cyan'} fill={H.lv >= 2} pulse={H.lv >= 2}>{H.lv >= 2 ? 'needs someone' : H.lv ? 'keep an eye' : 'all systems ok'}</Tag>
        <div style={{ fontFamily: 'ORB', fontWeight: 700, fontSize: 24, color: '#fff', textShadow: '0 0 10px rgba(255,255,255,0.5)', fontVariantNumeric: 'tabular-nums' }}>{clkFull(n).toUpperCase()}</div>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 300px', gap: 16 }}>
        <div style={{ display: 'grid', gap: 12, alignContent: 'start' }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 12 }}>{rooms.filter((r) => r.st !== 'closed').map((r) => <Room key={r.r} r={r} s={s} />)}</div>
          {rooms.filter((r) => r.st === 'closed').map((r) => <Prompt key={r.r} dim>{r.r.toLowerCase() + ': closed – renovation'}</Prompt>)}
        </div>
        <div style={{ display: 'grid', gap: 12, alignContent: 'start' }}>
          <Win title="up_next.exe">
            {G.length ? G.slice(0, 4).map((g, i) => <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 7, opacity: i ? 0.8 : 1 }}>
              {g.rs.slice(0, 4).map((r) => <Pix key={r} name={RICON[r]} color={RC[r]} size={13} glow />)}
              <span style={{ whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', minWidth: 0 }}>{(g.rs.length > 2 ? g.rs.length + ' rooms' : g.rs.join('+')) + (g.t === 'out' ? ' out ' : ' in ') + clk(g.at)}</span>
              <span style={{ flex: 1 }} /><Tag sm tone={i === 0 ? (g.at - n < 15 * 60e3 ? 'sun' : 'cyan') : 'dim'}>{left(g.at - n)}</Tag></div>)
              : <Prompt dim>nothing more tonight_</Prompt>}
          </Win>
          {M && <Win title="today.log" accent={MAG}>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: 9 }}>
              <span style={{ fontFamily: 'ORB', fontWeight: 900, fontSize: 30, color: '#fff', textShadow: '0 0 10px rgba(255,255,255,0.5)' }}>{M.n}</span><span style={{ color: DIM, letterSpacing: '.15em' }}>BOOKED</span>
              <span style={{ flex: 1 }} /><span style={{ fontFamily: 'ORB', fontWeight: 700, fontSize: 18, color: CYAN, textShadow: '0 0 8px rgba(0,255,255,0.8)' }}>{money(M.paid)}</span><span style={{ color: DIM, letterSpacing: '.15em' }}>PAID</span></div>
            {(M.nUnpaid > 0 || M.cx > 0) && <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', paddingLeft: 3 }}>
              {M.nUnpaid > 0 && <Tag sm tone="sun">{M.nUnpaid + ' not paid · ' + money(M.unpaid)}</Tag>}{M.cx > 0 && <Tag sm tone="mag">{M.cx + ' cancelled'}</Tag>}</div>}
            {M.last && <Prompt>{'latest: ' + M.last.r + (NAMES && M.last.n ? ' · ' + M.last.n : '') + ' · ' + ago(M.last.t, n)}</Prompt>}
          </Win>}
          {T && <Win title={'tomorrow' + (T.first ? ' · ' + dayName(T.first.at) : '')}>
            {!T.n ? <Prompt dim>nothing booked yet_</Prompt> : (T.list || [T.first]).slice(0, 7).map((x, i) => <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <span style={{ width: 60, color: CYAN }}>{clk(x.at)}</span><Pix name={RICON[x.r]} color={RC[x.r]} size={12} glow />
              <span style={{ color: RC[x.r], textShadow: `0 0 6px ${mix(RC[x.r], 0.7)}` }}>{x.r}</span><span style={{ color: DIM, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{(x.e ? 'till ' + clk(x.e) : '') + (NAMES && x.n ? ' · ' + x.n : '')}</span></div>)}
            {T.n > 7 && <Prompt dim>{'+' + (T.n - 7) + ' more'}</Prompt>}
          </Win>}
          <Win title="health.sys" accent={H.lv ? MAG : CYAN}>
            {H.list && H.list.length ? H.list.slice(0, 5).map((x, i) => <Prompt key={i} color={x.lv >= 2 ? MAG : SUN}>{x.t.replace(/^[^\w"]+/, '')}</Prompt>) : <Prompt color={CYAN}>all systems ok_</Prompt>}
            {H.sb != null && <div style={{ color: DIM, fontSize: 12 }}>{'simplybook ' + H.sb + ' / ~10,000 today'}</div>}
          </Win>
          <div style={{ fontSize: 11.5, letterSpacing: '.1em', color: stale || s.err ? SUN : DIM }}>{'> ' + (s.err ? s.err : 'updated ' + ago(s.at, Date.now()) + (j.age > 15 ? ' · engine copy ' + j.age + ' min old' : ''))}</div>
        </div>
      </div>
    </div>
  </div>);
};

export const className = `pointer-events: none;`;
