/* Round2 UI – React components for staff screens. Same look as the plain-HTML classes in r2-ui.css (load that CSS too).
   The design language and its rules: README.md in this folder. Reference design: the Mac board (widget/round2-board.jsx).

   import { Panel, Header, RoomGrid, RoomTile, Strip, Chip, Kick, Ring, Countdown, Line, Pix, Then,
            Button, Field, Toggle, Tabs, List, Notice, ThemeSwitch } from './ui/r2-ui.jsx';
*/
import React from 'react';
import { PIX, room, clk, left, hrs, setTheme, UI_NAME } from './r2-ui.js';
export { UI_NAME };

const cx = (...a) => a.filter(Boolean).join(' ');

/* ---------- small pieces ---------- */
export const Pix = ({ name, size = 14, color = 'currentColor' }) => {
  const P = PIX[name]; if (!P) return null;
  const rects = []; P.forEach((row, y) => { for (let x = 0; x < row.length; x++) if (row[x] === '#') rects.push(<rect key={x + '-' + y} x={x} y={y} width="1.02" height="1.02" />); });
  return <svg className="r2-pix" width={size} height={size * P.length / P[0].length} viewBox={`0 0 ${P[0].length} ${P.length}`} fill={color} aria-hidden="true">{rects}</svg>;
};
/** small game-font label above a group: TONIGHT, UP NEXT, LEFT */
export const Kick = ({ children, sm, as: T = 'div' }) => <T className={cx('r2-kick', sm && 'sm')}>{children}</T>;
/** a room's name in the game font and its colour, with its icon */
export const RoomName = ({ r, sm }) => { const R = room(r); return <span className={cx('r2-room', sm && 'sm', R.cls)}><Pix name={R.icon} size={sm ? 11 : 15} color="var(--c)" />{R.r}</span>; };
/** status box. state: 'ok' | 'wait' | 'bad' | 'next' (message/health dot) – or inside a room tile use dot (room colour).
    strong = outline in the colour; fill = light fill in the colour ("Ending") */
export const Chip = ({ children, state, dot, strong, fill, sm, className, onClick }) =>
  <span className={cx('r2-chip', state, dot && 'dot', strong && 'strong', fill && 'fill', sm && 'sm', onClick && 'tap', className)} onClick={onClick}
    role={onClick ? 'button' : undefined} tabIndex={onClick ? 0 : undefined}><span>{children}</span></span>;
/** pixel icon + text */
export const Line = ({ icon, children, bold, roomColour }) =>
  <div className={cx('r2-line', bold && 'bold', roomColour && 'room')}>{icon && <Pix name={icon} size={11} />}<span>{children}</span></div>;
/** the big number – ALWAYS a countdown with words saying what it counts to ("left", "cleaning left", "till arrival").
    quiet = not guests-in (grey); hot = ending soon (room colour) */
export const Countdown = ({ ms, label, quiet, hot }) =>
  <div className={cx('r2-count', quiet && 'quiet', hot && 'hot')}><b>{left(ms)}</b><Kick>{label}</Kick></div>;
/** the ring – ONE meaning: guests in. p = share of the booking used; booked = booking length (ms) */
export const Ring = ({ p, booked, label = 'booked', size = 92 }) => {
  const R = size / 2 - 3, C = 2 * Math.PI * R, q = Math.max(0, Math.min(1, +p || 0));
  return <div className="r2-ring" style={{ width: size, height: size, '--C': C }}>
    <svg width={size} height={size}><circle className="track" cx={size / 2} cy={size / 2} r={R} />
      {q > 0 && <circle className="used" cx={size / 2} cy={size / 2} r={R} strokeDasharray={`${C * q} ${C}`} />}</svg>
    <div className="mid"><b>{hrs(booked)}</b><Kick sm>{label}</Kick></div></div>;
};
/** who's next in this room after the current guest */
export const Then = ({ at, name, gap }) => <div className="r2-then"><Kick sm>then</Kick><b>{clk(at)}</b>{name && <span className="r2-sub">{name}</span>}
  {gap != null && <span className={cx('gap', gap < 30 && 'tight', gap >= 30 && 'r2-sub')} style={{ fontSize: 11.5 }}>{gap + ' min gap'}</span>}</div>;

/* ---------- layout ---------- */
export const Panel = ({ children, style }) => <div className="r2 r2-panel" style={style}>{children}</div>;
/** tap / onClick = it reacts to hover and press (rises, outline takes the room colour) */
export const Card = ({ r, lit, children, className, onClick, tap }) => <div className={cx('r2-card', r && room(r).cls, lit && 'lit', (tap || onClick) && 'tap', className)} onClick={onClick}>{children}</div>;
export const Rule = () => <hr className="r2-rule" />;
export const Header = ({ logo = 'ui/r2-logo.png', kick, line, right }) =>
  <div style={{ display: 'flex', alignItems: 'center', gap: 22 }}>
    <img className="r2-logo" src={logo} alt="Round2" />
    {(kick || line) && <div><Kick>{kick}</Kick><div className="r2-sub" style={{ marginTop: 7, fontWeight: 500, fontSize: 14 }}>{line}</div></div>}
    <div style={{ flex: 1 }} />{right}</div>;
export const RoomGrid = ({ children }) => <div className="r2-rooms">{children}</div>;
/** the quiet info row under the main view: each child is a column, split by thin lines */
export const Strip = ({ children }) => <div className="r2-strip">{children}</div>;
export const Col = ({ title, children }) => <div><Kick>{title}</Kick>{children}</div>;

/** A room tile, as on the Mac board. r = a room from the engine's /w answer ({ r, st, till, since, n, x, next, nn, gap, tv, ps4, msgs }).
    now = the engine's time (never the device clock). */
export const RoomTile = ({ r, now, names = true, onClick, tap }) => {
  const R = room(r.r), inn = r.st === 'in', soon = inn && r.till - now < 15 * 60e3;
  const tmr = r.next && new Date(r.next + 8 * 3600e3).getUTCDate() !== new Date(now + 8 * 3600e3).getUTCDate() && new Date(r.next + 8 * 3600e3).getUTCHours() >= 7;
  const chip = inn ? <Chip dot strong fill={soon}>{(soon ? 'Ending · ' : 'In · till ') + clk(r.till)}</Chip>
    : r.st === 'clean' ? <Chip dot>{'Cleaning till ' + clk(r.till)}</Chip>
    : r.st === 'next' ? <Chip dot>{'Next ' + clk(r.next) + (tmr ? ' tmr' : '')}</Chip> : <Chip>Free</Chip>;
  const cd = inn ? <Countdown ms={r.till - now} label="left" hot={soon} />
    : r.st === 'clean' ? <Countdown ms={r.till - now} label="cleaning left" quiet />
    : r.st === 'next' && r.next > now && r.next - now < 3 * 3600e3 ? <Countdown ms={r.next - now} label="till arrival" quiet /> : null;
  const who = inn ? r.n : r.st === 'next' ? r.nn : '';
  const tv = r.tv ? (r.tv.off ? 'TV off' : r.tv.quiet ? 'TV not reporting' : r.tv.app || 'TV on') : '';
  return <Card r={r.r} lit={inn} className="r2-tile" onClick={onClick} tap={tap}>
    <RoomName r={r.r} />
    <div className="body">
      <div className="main">
        <div>{chip}</div>{cd && <div key={r.st} className="r2-in">{cd}</div>}
        <div style={{ display: 'grid', gap: 5 }}>
          {names && who && <Line icon="user" bold>{who}</Line>}
          {inn && r.x ? <Line roomColour>Extended</Line> : null}
          {tv && (inn || (r.tv && !r.tv.off && !r.tv.quiet)) && <Line icon="tv">{tv}</Line>}
          {r.ps4 && r.ps4.on && <Line icon="pad">{r.ps4.g || 'PS4 menu'}</Line>}
        </div>
      </div>
      <div className="side">
        {inn && r.since && <Ring key={r.since} p={(now - r.since) / (r.till - r.since)} booked={r.till - r.since} />}
        {(inn || r.st === 'clean') && r.next && <Then at={r.next} name={names ? r.nn : ''} gap={tmr ? null : r.gap} />}
        {r.st === 'free' && <span className="r2-sub" style={{ fontSize: 12 }}>{r.next ? 'next ' + clk(r.next) + (tmr ? ' tmr' : '') : 'nothing more tonight'}</span>}
      </div>
    </div>
    {r.msgs && r.msgs.length > 0 && <div className="msgs">{r.msgs.slice(0, 4).map((m) => {
      const st = m.ok ? 'ok' : m.at > now || m.ak === 0 ? 'next' : 'wait', auto = m.k === 'wrapup' || m.k === 'timeup';
      return <Chip key={m.at + (m.k || '') + (m.ti || '')} sm state={st}>{auto ? (m.k === 'wrapup' ? '10m ' : 'Wrap ') + clk(m.at) : clk(m.at) + ' ' + (m.ti || 'TV message').slice(0, 26)}{m.ok ? ' ✓' : ''}</Chip>;
    })}</div>}
  </Card>;
};

/* ---------- controls ---------- */
/** variant: undefined (outline) | 'solid' (the ONE main action on a screen) | 'quiet' | 'danger'; r = room colour outline */
export const Button = ({ children, variant, r, ...p }) => <button className={cx('r2-btn', variant, r && 'room', r && room(r).cls)} {...p}>{children}</button>;
export const Field = ({ label, ...p }) => <label className="r2-field"><Kick>{label}</Kick><input className="r2-input" {...p} /></label>;
export const Toggle = ({ label, checked, onChange, r }) =>
  <label className={cx('r2-toggle', r && room(r).cls)}><input type="checkbox" checked={!!checked} onChange={(e) => onChange && onChange(e.target.checked)} /><i />{label}</label>;
export const Tabs = ({ tabs, value, onChange }) => <div className="r2-tabs" role="tablist">{tabs.map((t) =>
  <button key={t} role="tab" className="r2-tab" aria-selected={t === value} onClick={() => onChange && onChange(t)}>{t}</button>)}</div>;
export const List = ({ children, tap }) => <div className={cx('r2-list', tap && 'tap')}>{children}</div>;
/** a message for staff. state: 'ok' | 'wait' | 'bad' */
export const Notice = ({ state, title, children }) => <div className={cx('r2-notice', state)} role={state === 'bad' ? 'alert' : undefined}><div><b>{title}</b><span>{children}</span></div></div>;
/** dark / light / auto – sets <html data-theme> and remembers it on this device */
export const ThemeSwitch = ({ value, onChange }) => <Tabs tabs={['dark', 'light', 'auto']} value={value} onChange={(m) => { setTheme(m); onChange && onChange(m); }} />;
