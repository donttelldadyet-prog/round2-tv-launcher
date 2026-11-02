/* ONE CLOCK + THE SHOP NIGHT for every control-centre screen (owner, 2 Nov 2026) – docs/system-check.md "Screens".
   R2Day.now()       the engine's time (Cloudflare /time; the control centre keeps the offset in localStorage r2.clockOff),
                     never this phone's own clock.
   R2Day.night(ms)   the shop night a moment belongs to, 'YYYY-MM-DD', Singapore time: before 7am it is still the night
                     before (staff wake ~10am and want today's list on the way in).
   R2Day.tonight()   night(now()).        R2Day.addDays('YYYY-MM-DD', n)
   R2Day.list(name, { night: fn(ms), today: fn(nowMs) })
                     EVERY list that files things under a day or shows "today / tonight" registers here, passing the very
                     functions it draws with. System check › "Screens follow the 7am rule" runs each one with made-up times
                     around 7am and midnight, and its watchdog flags day maths written anywhere without this file. */
(function () {
  var H = 3600e3;
  function off() { try { return Number(localStorage.getItem('r2.clockOff')) || 0; } catch (e) { return 0; } }
  var R = window.R2Day = {
    SWITCH: 7,
    now: function () { return Date.now() + off(); },
    night: function (ms) { return new Date(+ms + 8 * H - R.SWITCH * H).toISOString().slice(0, 10); },
    tonight: function () { return R.night(R.now()); },
    addDays: function (d, n) { var x = new Date(d + 'T00:00:00Z'); x.setUTCDate(x.getUTCDate() + n); return x.toISOString().slice(0, 10); },
    lists: {},
    list: function (name, f) { R.lists[name] = f; }
  };
}());
