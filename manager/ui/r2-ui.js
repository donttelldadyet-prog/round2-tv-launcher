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
  return `<div class="r2-ring" style="width:${size}px;height:${size}px"><svg width="${size}" height="${size}"><circle class="track" cx="${size / 2}" cy="${size / 2}" r="${R}"/>` +
    (q > 0 ? `<circle class="used" cx="${size / 2}" cy="${size / 2}" r="${R}" stroke-dasharray="${C * q} ${C}"/>` : '') + `</svg><div class="mid">${mid}</div></div>`;
}

/* times – Singapore time always (the shop's one clock: pass the engine's time, never the device's) */
const sgd = (ms) => new Date(ms + 8 * 3600e3);
export const clk = (ms) => { const d = sgd(ms), h = d.getUTCHours(), m = d.getUTCMinutes(); return ((h + 11) % 12 + 1) + (m ? ':' + String(m).padStart(2, '0') : '') + (h < 12 ? 'am' : 'pm'); };
export const left = (ms) => { const t = Math.max(0, Math.round(ms / 1000)), h = Math.floor(t / 3600), m = Math.floor(t % 3600 / 60), x = t % 60; return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(x).padStart(2, '0'); };
export const hrs = (ms) => { const m = Math.round(ms / 60e3), h = Math.floor(m / 60), x = m % 60; return (h ? h + 'h' : '') + (x ? (h ? ' ' : '') + x + 'm' : h ? '' : '0m'); };

/* light / dark: 'dark' | 'light' | 'auto' (follows the device). Remembered per device. */
export function setTheme(mode) {
  try { localStorage.setItem('r2-theme', mode); } catch (e) {}
  document.documentElement.setAttribute('data-theme', mode);
}
export function initTheme(def = 'auto') {
  let m = def; try { m = localStorage.getItem('r2-theme') || def; } catch (e) {}
  document.documentElement.setAttribute('data-theme', m); return m;
}

if (typeof window !== 'undefined') window.R2UI = { ROOMS, room, PIX, pixSvg, ringSvg, clk, left, hrs, setTheme, initTheme };
