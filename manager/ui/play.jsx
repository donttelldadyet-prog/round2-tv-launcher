/* The live preview next to the style guide (index.html › right-hand panel). Built with the React pieces (r2-ui.jsx), so
   it shows exactly how they move. The Motion lab edits the motion tokens of r2-ui.css live – on this page only – and
   "Copy settings" gives the values to put in r2-ui.css (or to send to Claude).
   Build after editing: node ui/build.cjs   (makes play.bundle.js – React is bundled in, the page needs no internet) */
import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { RoomTile, Chip, Button, Toggle, Tabs, Field, List, Notice, Kick, RoomName, UI_NAME } from './r2-ui.jsx';
import { ROOMS, clk, setTheme } from './r2-ui.js';

/* ---------- the motion tokens the lab can change (defaults = r2-ui.css) ---------- */
const EASES = {
  Calm: 'cubic-bezier(.2, .8, .2, 1)', Soft: 'cubic-bezier(.4, 0, .2, 1)', Quick: 'cubic-bezier(.05, .9, .1, 1)',
  Springy: 'cubic-bezier(.3, 1.4, .5, 1)', Linear: 'linear',
};
const KNOBS = [
  { k: '--r2-dur', label: 'Hover & press speed', min: 40, max: 600, step: 10, unit: 'ms', def: 260 },
  { k: '--r2-dur-slow', label: 'State changes & appearing', min: 100, max: 1400, step: 20, unit: 'ms', def: 600 },
  { k: '--r2-lift', label: 'Rise on hover', min: -8, max: 0, step: 0.5, unit: 'px', def: -1.5 },
  { k: '--r2-press', label: 'Shrink while pressed', min: 0.88, max: 1, step: 0.01, unit: '', def: 0.98 },
  { k: '--r2-hover-tint', label: 'Room colour on tile hover', min: 0, max: 100, step: 5, unit: '%', def: 35 },
  { k: '--r2-ring-draw', label: 'Ring drawing in', min: 0, max: 2400, step: 50, unit: 'ms', def: 1200 },
  { k: '--r2-breathe', label: '"Ending" dot breathing', min: 0.6, max: 6, step: 0.1, unit: 's', def: 3.2 },
];
const DEF = Object.fromEntries(KNOBS.map((x) => [x.k, x.def]).concat([['ease', 'Soft']]));
const load = () => { try { return Object.assign({}, DEF, JSON.parse(localStorage.getItem('r2-motion') || '{}')); } catch (e) { return { ...DEF }; } };
const cssOf = (v) => ':root {\n' + KNOBS.map((x) => `  ${x.k}: ${v[x.k]}${x.unit};`).join('\n') + `\n  --r2-ease: ${EASES[v.ease] || v.ease};\n}`;

function MotionLab() {
  const [v, setV] = useState(load), [slow, setSlow] = useState(1), [on, setOn] = useState(true), [copied, setCopied] = useState('');
  useEffect(() => {
    const st = document.documentElement.style;
    for (const x of KNOBS) st.setProperty(x.k, v[x.k] + x.unit);
    st.setProperty('--r2-ease', EASES[v.ease] || v.ease);
    try { localStorage.setItem('r2-motion', JSON.stringify(v)); } catch (e) {}
  }, [v]);
  useEffect(() => { document.documentElement.style.setProperty('--r2-slow', slow); }, [slow]);
  useEffect(() => { document.documentElement.setAttribute('data-motion', on ? 'on' : 'off'); }, [on]);
  const changed = KNOBS.some((x) => v[x.k] !== x.def) || v.ease !== 'Soft';
  const copy = () => { const t = cssOf(v); setCopied(t); try { navigator.clipboard.writeText(t).catch(() => {}); } catch (e) {} };
  return <div style={{ display: 'grid', gap: 14 }}>
    <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}><Kick>Motion lab</Kick><span style={{ flex: 1 }} />
      <Toggle label="Motion" checked={on} onChange={setOn} /></div>
    <div>
      <div className="r2-sub" style={{ fontSize: 12, marginBottom: 7 }}>Feel</div>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>{Object.keys(EASES).map((e) =>
        <Chip key={e} sm state={v.ease === e ? 'ok' : undefined} strong={v.ease === e} onClick={() => setV({ ...v, ease: e })}>{e}</Chip>)}</div>
    </div>
    {KNOBS.map((x) => <label key={x.k} className="lab-knob">
      <span className="lab-row"><span>{x.label}</span><b>{v[x.k]}{x.unit}</b></span>
      <input type="range" min={x.min} max={x.max} step={x.step} value={v[x.k]} onChange={(e) => setV({ ...v, [x.k]: +e.target.value })} />
    </label>)}
    <div>
      <div className="r2-sub" style={{ fontSize: 12, marginBottom: 7 }}>Slow motion – to watch an animation closely</div>
      <Tabs tabs={['×1', '×4', '×10']} value={'×' + slow} onChange={(t) => setSlow(+t.slice(1))} />
    </div>
    <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
      <Button variant="solid" onClick={copy}>Copy settings</Button>
      <Button variant="quiet" disabled={!changed} onClick={() => { setV({ ...DEF }); setCopied(''); }}>Reset</Button>
    </div>
    {copied && <pre className="lab-out r2-in">{copied}</pre>}
    <div className="r2-sub" style={{ fontSize: 11.5 }}>Changes stay on this device only, until they're copied into the UI's files.</div>
  </div>;
}

/* ---------- things to touch ---------- */
const STATES = ['In', 'Ending', 'Cleaning', 'Arriving', 'Free'];
function sampleRoom(r, st, now, since) {
  const m = 60e3;
  if (st === 'In') return { r, st: 'in', since, till: since + 3 * 3600e3, n: 'Theodore', tv: { app: 'Netflix' }, ps4: { on: 1, g: 'Gran Turismo 7' }, next: since + 3 * 3600e3 + 35 * m, nn: 'Raph', gap: 20,
    msgs: [{ at: since + 3 * 3600e3 - 10 * m, k: 'wrapup', ak: 1 }, { at: since + 3 * 3600e3, k: 'timeup', ak: 1 }] };
  if (st === 'Ending') return { r, st: 'in', since: now - 171 * m, till: now + 9 * m, n: 'Sakura', x: 1, tv: { app: 'PS4 / HDMI' }, ps4: { on: 1, g: 'EA FC 25' },
    msgs: [{ at: now - m, k: 'wrapup', ok: now, ak: 1 }, { at: now + 9 * m, k: 'timeup', ak: 1 }] };
  if (st === 'Cleaning') return { r, st: 'clean', till: now + 11 * m, next: now + 95 * m, nn: 'Priya', gap: 80 };
  if (st === 'Arriving') return { r, st: 'next', next: now + 46 * m, nn: 'daren', msgs: [{ at: now + 2 * 3600e3, k: 'guest', ti: 'Happy birthday Daren!', ak: 1 }] };
  return { r, st: 'free', next: now + 6 * 3600e3 };
}

function Preview() {
  const [now, setNow] = useState(Date.now()), [since] = useState(Date.now() - 40 * 60e3);
  const [r, setR] = useState('Kai'), [st, setSt] = useState('In'), [theme, setT] = useState(() => document.documentElement.getAttribute('data-theme') || 'dark');
  const [tab, setTab] = useState('Tonight'), [tg, setTg] = useState({ a: true, b: false }), [msg, setMsg] = useState(['next', 'wait', 'ok']);
  const [notes, setNotes] = useState([]), [pressed, setPressed] = useState('');
  useEffect(() => { const t = setInterval(() => setNow(Date.now()), 1000); return () => clearInterval(t); }, []);
  /* the style guide's own dark/light switch changes the page too – follow it */
  useEffect(() => { const o = new MutationObserver(() => setT(document.documentElement.getAttribute('data-theme'))); o.observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] }); return () => o.disconnect(); }, []);
  const cycle = (i) => setMsg(msg.map((s, j) => j === i ? ({ next: 'wait', wait: 'ok', ok: 'next' })[s] : s));
  const N = [['ok', 'Kai\'s helper is running again', 'Restarted by the TV itself.'], ['wait', 'Bella TV not reporting', 'Last check-in 24 min ago.'], ['bad', 'Payment not matched', 'S$96 paid, no booking found.']];
  return <div style={{ display: 'grid', gap: 26 }}>
    <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
      <Kick>Live preview</Kick><span style={{ flex: 1 }} />
      <Tabs tabs={['dark', 'light']} value={theme} onChange={(m) => { setTheme(m); setT(m); }} />
    </div>

    <section className="pv">
      <Kick>Room tile – hover it, switch its state</Kick>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>{ROOMS.filter((x) => x.r !== 'Kaeden').map((x) =>
        <Chip key={x.r} sm dot className={x.cls} strong={x.r === r} onClick={() => setR(x.r)}>{x.r}</Chip>)}</div>
      <Tabs tabs={STATES} value={st} onChange={setSt} />
      <RoomTile r={sampleRoom(r, st, now, since)} now={now} tap />
    </section>

    <section className="pv">
      <Kick>Buttons – hover and press</Kick>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
        <Button variant="solid" onClick={() => setPressed('Save')}>Save</Button>
        <Button onClick={() => setPressed('Send to TV')}>Send to TV</Button>
        <Button r={r} onClick={() => setPressed(r + ' TV')}>{r} TV</Button>
        <Button variant="quiet" onClick={() => setPressed('Cancel')}>Cancel</Button>
        <Button variant="danger" onClick={() => setPressed('Cancel booking')}>Cancel booking</Button>
        <Button disabled>Not now</Button>
      </div>
      <div className="r2-sub" style={{ fontSize: 12, minHeight: 16 }}>{pressed && <span key={pressed + now} className="r2-in" style={{ display: 'inline-block' }}>Pressed: {pressed}</span>}</div>
    </section>

    <section className="pv">
      <Kick>TV message chips – tap to change state</Kick>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
        {['10m ' + clk(now + 20 * 60e3), 'Wrap ' + clk(now + 30 * 60e3), clk(now + 3600e3) + ' Happy birthday!'].map((t, i) =>
          <Chip key={i} sm state={msg[i]} onClick={() => cycle(i)}>{t + (msg[i] === 'ok' ? ' ✓' : msg[i] === 'wait' ? ' …' : '')}</Chip>)}
      </div>
      <div className="r2-sub" style={{ fontSize: 12 }}>grey = coming · orange = on the TV, waiting for OK · green = OK'd</div>
    </section>

    <section className="pv">
      <Kick>Switches, tabs, typing</Kick>
      <Toggle label="Auto wrap-up messages" checked={tg.a} onChange={(c) => setTg({ ...tg, a: c })} />
      <Toggle r={r} label={r + ' Home-button helper'} checked={tg.b} onChange={(c) => setTg({ ...tg, b: c })} />
      <Tabs tabs={['Tonight', 'Messages', 'TVs']} value={tab} onChange={setTab} />
      <Field label="Guest name" placeholder="e.g. Theodore" />
    </section>

    <section className="pv">
      <Kick>List rows – hover</Kick>
      <List tap>{[['Kai', '9:51pm', 'Jess', 'ok', 'paid'], ['Max', '10:51pm', 'Ravi', 'wait', 'not paid'], ['Bella', '1:51am', 'Mei', 'ok', 'paid']].map(([rr, t, n, s, p]) =>
        <div key={rr}><b style={{ width: 58 }}>{t}</b><RoomName r={rr} sm /><span>{n}</span><span style={{ flex: 1 }} /><Chip sm state={s}>{p}</Chip></div>)}</List>
    </section>

    <section className="pv">
      <Kick>Notices – how they appear</Kick>
      <div style={{ display: 'flex', gap: 8 }}>
        <Button onClick={() => setNotes([{ id: now + Math.random(), n: N[notes.length % 3] }, ...notes].slice(0, 3))}>Show a notice</Button>
        <Button variant="quiet" disabled={!notes.length} onClick={() => setNotes([])}>Clear</Button>
      </div>
      {notes.map((x) => <Notice key={x.id} state={x.n[0]} title={x.n[1]}>{x.n[2]}</Notice>)}
    </section>

    <section className="pv lab"><MotionLab /></section>
    <div className="r2-sub" style={{ fontSize: 11 }}>{UI_NAME} · name pending · sample guests are made up</div>
  </div>;
}

createRoot(document.getElementById('play')).render(<Preview />);
