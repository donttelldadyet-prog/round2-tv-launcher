/* Round2 UI – shared pieces for staff screens (plain JS, no framework). Load as a module:
     <script type="module" src="ui/r2-ui.js"></script>   → also available as window.R2UI for older page code
   The React versions (r2-ui.jsx) use the same data and the same CSS classes (r2-ui.css). */

/* the rooms: name, CSS class (sets the room colour), pixel icon. Kaeden: under renovation since ~30 Oct 2026 */
export const ROOMS = [
  { r: 'Vale', cls: 'r2-vale', icon: 'cat' }, { r: 'Kaeden', cls: 'r2-kaeden', icon: 'leaf' }, { r: 'Scarlett', cls: 'r2-scarlett', icon: 'saturn' },
  { r: 'Kai', cls: 'r2-kai', icon: 'mount' }, { r: 'Bella', cls: 'r2-bella', icon: 'book' }, { r: 'Atlas', cls: 'r2-atlas', icon: 'boat' },
  { r: 'Max', cls: 'r2-max', icon: 'car' },
];
export const room = (name) => ROOMS.find((x) => x.r.toLowerCase() === String(name || '').toLowerCase().replace(/^maximillion$/, 'max')) || { r: name, cls: '', icon: 'tv' };

/* pixel icons (12×12 grids, '#' = on) – the same set as the TV menu, iPhone widget and Mac board */
export const PIX = {"tv":["..#.....#...","...#...#....","....#.#.....","############","#..........#","#...#......#","#...###....#","#...#####..#","#...###....#","#...#......#","#..........#","############"],"clock":["...######...","..#......#..",".#...#....#.","#....#.....#","#....#.....#","#....###...#","#.......#..#","#........#.#","#..........#",".#........#.","..#......#..","...######..."],"user":["....####....","...#....#...","...#....#...","...#....#...","....####....","............","..########..",".#........#.","#..........#","#..........#","#..........#","############"],"cake":[".....##.....","......#.....",".....##.....",".....##.....",".##########.",".#........#.",".#.##..##.#.",".##########.","#..........#","#.#.#..#.#.#","#..........#","############"],"cat":["............",".#.......#..",".##.....##..",".#.#####.#..",".#.......#..","#..#...#..#.","#.........#.","#....#....#.",".#..#.#..#..","..#.....#...","...#####....","............"],"mount":["............","............",".....#......","....#.#.....","...#...#.#..","..#.#.#.#.#.","..#..#..#..#",".#.........#",".#.........#","#...........","############","............"],"book":["............",".####..####.","#....##....#","#.##.##.##.#","#....##....#","#.##.##.##.#","#....##....#","#.##.##.##.#","#....##....#",".####..####.",".....##.....","............"],"boat":[".....#......",".....##.....",".....###....",".....####...",".....#####..",".....######.",".....#......","############",".##########.","..########..","............","#.##.##.##.#"],"car":["............","............","...######...","..#..#...#..",".#...#....#.","############","#..........#","#.##....##.#","############",".##......##.",".##......##.","............"],"saturn":["..............","..............",".....####.....","....######..##","...#########.#","...#######.##.","...#####..#...","...##...###...",".##..######...","#.#########...","##..######....",".....####.....","..............",".............."],"leaf":[".........###","......######","....#####..#","...####...#.","..####...#..","..###...#...",".###...#....",".##...#.....",".#.###......",".###........","#...........","............"],"moon":["....####....","..###.......",".##.........",".#..........","##..........","##..........","##..........","##.........#",".##.......##",".###.....##.","..########..","....####...."],"pad":["............","..########..",".#........#.","#..#....#..#","#.###..#.#.#","#..#....#..#","#..........#","#...####...#",".#.#....#.#.","..#......#..","............","............"],"bell":[".....##.....","....####....","...######...","...######...","..########..","..########..",".##########.","############","............",".....##.....","............","............"],"warn":[".....##.....","....####....","....#..#....","...##..##...","...##..##...","..###..###..","..###..###..",".##########.",".####..####.","#####..#####","############","............"],"check":["............","...........#","..........##",".........##.","#.......##..","##.....##...",".##...##....","..##.##.....","...###......","....#.......","............","............"],"sparkle":["......#.....","......#.....",".....###....","###########.",".....###....","......#.....","..#...#.....","..#.........",".###......#.","..#......###","..#.......#.","............"]};

/* <svg> for a pixel icon; colour defaults to the text colour (currentColor) */
export function pixSvg(name, size = 14, color = 'currentColor') {
  const P = PIX[name]; if (!P) return '';
  let r = ''; P.forEach((row, y) => { for (let x = 0; x < row.length; x++) if (row[x] === '#') r += `<rect x="${x}" y="${y}" width="1.02" height="1.02"/>`; });
  return `<svg class="r2-pix" width="${size}" height="${size * P.length / P[0].length}" viewBox="0 0 ${P[0].length} ${P.length}" fill="${color}" aria-hidden="true">${r}</svg>`;
}

/* the ring: p = 0…1 used; mid = HTML for the middle (e.g. '<b>3h</b><span class="r2-kick sm">booked</span>') */
export function ringSvg(p, mid = '', size = 92) {
  const R = size / 2 - 3, C = 2 * Math.PI * R, q = Math.max(0, Math.min(1, +p || 0));
  return `<div class="r2-ring" style="width:${size}px;height:${size}px;--C:${C}"><svg width="${size}" height="${size}"><circle class="track" cx="${size / 2}" cy="${size / 2}" r="${R}"/>` +
    (q > 0 ? `<circle class="used" cx="${size / 2}" cy="${size / 2}" r="${R}" stroke-dasharray="${C * q} ${C}"/>` : '') + `</svg><div class="mid">${mid}</div></div>`;
}

/* times – Singapore time always (the shop's one clock: pass the engine's time, never the device's) */
const sgd = (ms) => new Date(ms + 8 * 3600e3);
export const clk = (ms) => { const d = sgd(ms), h = d.getUTCHours(), m = d.getUTCMinutes(); return ((h + 11) % 12 + 1) + (m ? ':' + String(m).padStart(2, '0') : '') + (h < 12 ? 'am' : 'pm'); };
export const left = (ms) => { const t = Math.max(0, Math.round(ms / 1000)), h = Math.floor(t / 3600), m = Math.floor(t % 3600 / 60), x = t % 60; return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(x).padStart(2, '0'); };
export const hrs = (ms) => { const m = Math.round(ms / 60e3), h = Math.floor(m / 60), x = m % 60; return (h ? h + 'h' : '') + (x ? (h ? ' ' : '') + x + 'm' : h ? '' : '0m'); };

/* The UI's name is still to be decided (owner, 25 Nov) – change it here and in the page titles */
export const UI_NAME = 'Round2 UI';

/* light / dark: 'dark' | 'light' | 'auto' (follows the device). Remembered per device. */
export function setTheme(mode) {
  try { localStorage.setItem('r2-theme', mode); } catch (e) {}
  document.documentElement.setAttribute('data-theme', mode);
}
export function initTheme(def = 'auto') {
  let m = def; try { m = localStorage.getItem('r2-theme') || def; } catch (e) {}
  document.documentElement.setAttribute('data-theme', m); return m;
}

/* ---- pop-ups (26 Nov) ---- */
const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
/* ask(text, { ok, cancel, danger }) → Promise<true/false>. The first paragraph is the question, the rest the details.
   Enter = yes, Esc / tapping outside = no. It closes with the same motion it opened with. */
export function ask(text, o = {}) {
  return new Promise((done) => {
    const parts = String(text).split(/\n\s*\n/), q = parts.shift(), back = document.createElement('div');
    back.className = 'r2-ask-back';
    back.innerHTML = `<div class="r2-ask" role="alertdialog" aria-modal="true" aria-labelledby="r2AskQ"><div class="r2-ask-q" id="r2AskQ" role="heading" aria-level="2">${esc(q)}</div>` +
      (parts.length ? `<p>${esc(parts.join('\n\n'))}</p>` : '') +
      `<div class="r2-ask-btns"><button type="button" class="r2-btn quiet" data-a="0">${esc(o.cancel || 'Cancel')}</button>` +
      `<button type="button" class="r2-btn ${o.danger ? 'danger' : 'solid'}" data-a="1">${esc(o.ok || 'Yes, go ahead')}</button></div></div>`;
    const prev = document.activeElement;
    const close = (v) => { document.removeEventListener('keydown', key, true); back.classList.add('out');
      setTimeout(() => { back.remove(); try { prev && prev.focus && prev.focus(); } catch (e) {} }, 220); done(v); };
    const key = (e) => { if (e.key === 'Escape') { e.preventDefault(); close(false); } else if (e.key === 'Enter') { e.preventDefault(); close(true); } };
    back.addEventListener('click', (e) => { const b = e.target.closest('[data-a]'); if (b) close(b.getAttribute('data-a') === '1'); else if (e.target === back) close(false); });
    document.addEventListener('keydown', key, true);
    document.body.appendChild(back);
    back.querySelector('[data-a="1"]').focus();
  });
}
/* toast(text) – a short note at the bottom for 3 seconds */
let toastEl = null, toastT = 0;
export function toast(text, ms = 3200) {
  if (!toastEl) { toastEl = document.createElement('div'); toastEl.className = 'r2-toast'; toastEl.setAttribute('role', 'status'); }
  toastEl.textContent = text; toastEl.classList.remove('out'); toastEl.remove(); void toastEl.offsetWidth; document.body.appendChild(toastEl);
  clearTimeout(toastT); toastT = setTimeout(() => { toastEl.classList.add('out'); setTimeout(() => toastEl.remove(), 200); }, ms);
}
/* enter(el) – a page that has just opened rises in (its .r2-rooms / .r2-stagger children one after another) */
export function enter(el) { if (!el) return; el.classList.remove('r2-enter'); void el.offsetWidth; el.classList.add('r2-enter'); clearTimeout(el.__r2e); el.__r2e = setTimeout(() => el.classList.remove('r2-enter'), 900); }

if (typeof window !== 'undefined') window.R2UI = { ask, toast, enter, UI_NAME, ROOMS, room, PIX, pixSvg, ringSvg, clk, left, hrs, setTheme, initTheme };
