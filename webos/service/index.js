/*
 * Round2 Home for LG webOS: background service that downloads event backgrounds (themes).
 * The LG version of app/.../ThemeSync.java. The menu calls luna://sg.round2.tvhome.service/sync
 * with { room, subscribe: true } and gets { json } back (the same JSON the Android app hands the menu),
 * first straight away from what's already on the TV, then again once downloads finish ({ done: true }).
 *
 * Written in old-style JavaScript on purpose: 2018-2020 LG TVs run an old Node.js.
 */
var Service = require('webos-service');
var fs = require('fs');
var path = require('path');
var https = require('https');
var crypto = require('crypto');
var urlParse = require('url').parse;

var MANIFEST_URL = 'https://github.com/donttelldadyet-prog/round2-tv-launcher/releases/download/themes/themes.json';
var EARLY_DAYS = 14;
var APP_ID = 'sg.round2.tvhome';
var service = new Service(APP_ID + '.service');

/* Where downloads are kept. Best: inside the app's own folder, so the menu can load them as themes/<id>.mp4.
   If the TV doesn't let the service write there, the service's own folder; if neither, the menu streams them. */
var STORE = null;
(function pickStore() {
  var tries = [
    { dir: path.join(__dirname, '..', '..', 'applications', APP_ID, 'themes'), url: function (f) { return 'themes/' + f; } },
    { dir: path.join(__dirname, 'themes'), url: function (f) { return 'file://' + path.join(__dirname, 'themes', f); } }
  ];
  for (var i = 0; i < tries.length; i++) {
    try {
      if (!fs.existsSync(tries[i].dir)) fs.mkdirSync(tries[i].dir);
      var probe = path.join(tries[i].dir, '.w');
      fs.writeFileSync(probe, '1'); fs.unlinkSync(probe);
      STORE = tries[i]; return;
    } catch (e) { /* not writable here */ }
  }
}());

function safeId(id) { return typeof id === 'string' && /^[a-z0-9][a-z0-9-]{0,40}$/.test(id) ? id : null; }
function pad(n) { return n < 10 ? '0' + n : '' + n; }
function day(d) { return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()); }
function forRoom(t, room) {
  var rooms = t.rooms || [];
  if (!rooms.length) return true;
  for (var i = 0; i < rooms.length; i++) if (String(rooms[i]).toLowerCase() === 'all' || String(rooms[i]).toLowerCase() === room) return true;
  return false;
}

/* HTTPS GET following GitHub's redirects; streams to a file when `file` is given */
function get(url, file, cb, hops) {
  hops = hops || 0;
  if (hops > 6 || url.indexOf('https://') !== 0) { cb(new Error('bad url')); return; }
  var o = urlParse(url);                                           /* (url, options, cb) needs Node 10; old TVs don't have it */
  var req = https.get({ hostname: o.hostname, port: o.port || 443, path: o.path, headers: { 'User-Agent': 'Round2Home-webOS' } }, function (res) {
    if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
      res.resume(); get(res.headers.location, file, cb, hops + 1); return;
    }
    if (res.statusCode !== 200) { res.resume(); cb(new Error('HTTP ' + res.statusCode)); return; }
    if (file) {
      var out = fs.createWriteStream(file), failed = false;
      out.on('error', function (e) { failed = true; res.resume(); cb(e); });
      out.on('finish', function () { if (!failed) cb(null); });
      res.pipe(out);
    } else {
      var parts = [];
      res.on('data', function (c) { parts.push(c); });
      res.on('end', function () { cb(null, Buffer.concat(parts)); });
    }
  });
  req.setTimeout(60000, function () { req.abort(); });
  req.on('error', function (e) { cb(e); });
}

function sha256(file, cb) {
  var h = crypto.createHash('sha256'), s = fs.createReadStream(file);
  s.on('data', function (c) { h.update(c); });
  s.on('end', function () { cb(h.digest('hex')); });
  s.on('error', function () { cb(''); });
}

/* download one file unless it's already here and intact */
function fetchFile(url, name, sha, cb) {
  if (!STORE || !url || url.indexOf('https://') !== 0) { cb(); return; }
  var dest = path.join(STORE.dir, name), part = dest + '.part';
  function download() {
    get(url, part, function (err) {
      if (err) { try { fs.unlinkSync(part); } catch (e) {} cb(); return; }
      if (!sha) { finish(); return; }
      sha256(part, function (h) {
        if (h.toLowerCase() !== String(sha).toLowerCase()) { try { fs.unlinkSync(part); } catch (e) {} cb(); return; }   /* damaged */
        finish();
      });
    });
  }
  function finish() { try { if (fs.existsSync(dest)) fs.unlinkSync(dest); fs.renameSync(part, dest); } catch (e) {} cb(); }
  if (!fs.existsSync(dest)) { download(); return; }
  if (!sha) { cb(); return; }
  sha256(dest, function (h) { if (h.toLowerCase() === String(sha).toLowerCase()) cb(); else download(); });
}

/* the list for the menu: each theme marked ready, pointing at the copy on the TV (or the online copy) */
function describe(root) {
  var ts = root.themes || [], ss = root.sounds || [], i, t, id;
  for (i = 0; i < ts.length; i++) {
    t = ts[i]; id = safeId(t.id); if (!id) continue;
    var v = STORE && fs.existsSync(path.join(STORE.dir, id + '.mp4'));
    var j = STORE && fs.existsSync(path.join(STORE.dir, id + '.jpg'));
    t.localVideo = v ? STORE.url(id + '.mp4') : (t.video || '');
    t.localThumb = j ? STORE.url(id + '.jpg') : (t.thumb || '');
    t.ready = !!t.localVideo;
  }
  for (i = 0; i < ss.length; i++) {
    t = ss[i]; id = safeId(t.id); if (!id) continue;
    if (STORE && fs.existsSync(path.join(STORE.dir, id + '.ogg'))) t.audio = STORE.url(id + '.ogg');
    if (STORE && fs.existsSync(path.join(STORE.dir, id + '.wav'))) t.pcm = STORE.url(id + '.wav'); else delete t.pcm;
    t.ready = !!t.audio;
  }
  return JSON.stringify(root);
}

function cached() {
  try {
    if (STORE) return describe(JSON.parse(fs.readFileSync(path.join(STORE.dir, 'themes.json'), 'utf8')));
  } catch (e) {}
  return null;
}

var running = false, waiting = [];
function sync(room, cb) {
  waiting.push(cb);
  if (running) return;
  running = true;
  function done(json) { running = false; var w = waiting; waiting = []; for (var i = 0; i < w.length; i++) w[i](json); }
  get(MANIFEST_URL, null, function (err, body) {
    if (err) { done(cached()); return; }                         /* offline: keep what's here */
    var root; try { root = JSON.parse(body.toString('utf8')); } catch (e) { done(cached()); return; }
    if (STORE) { try { fs.writeFileSync(path.join(STORE.dir, 'themes.json'), body); } catch (e) {} }
    var today = day(new Date()), soon = new Date(); soon.setDate(soon.getDate() + EARLY_DAYS);
    var horizon = day(soon), jobs = [], keep = { 'themes.json': 1 }, wantSound = {};
    (root.themes || []).forEach(function (t) {
      var id = safeId(t.id);
      if (!id || !forRoom(t, room)) return;
      if (t.until && t.until < today) return;                       /* ended */
      if (t.from && t.from > horizon) return;                       /* too far ahead */
      keep[id + '.mp4'] = keep[id + '.jpg'] = 1;
      if (safeId(t.ambience)) wantSound[t.ambience] = 1;
      jobs.push([t.thumb, id + '.jpg', null], [t.video, id + '.mp4', t.sha256]);
    });
    (root.sounds || []).forEach(function (t) {
      var id = safeId(t.id);
      if (!id || !(forRoom(t, room) || wantSound[id])) return;
      keep[id + '.ogg'] = keep[id + '.wav'] = 1;
      jobs.push([t.audio, id + '.ogg', t.sha256]);
      if (t.pcm) jobs.push([t.pcm, id + '.wav', t.pcmSha256]);
    });
    (function next() {                                              /* one at a time: TVs have little memory */
      var j = jobs.shift();
      if (j) { fetchFile(j[0], j[1], j[2], next); return; }
      if (STORE) {
        try {
          fs.readdirSync(STORE.dir).forEach(function (f) { if (!keep[f]) { try { fs.unlinkSync(path.join(STORE.dir, f)); } catch (e) {} } });
        } catch (e) {}
      }
      done(describe(root));
    }());
  });
}

service.register('sync', function (message) {
  var room = String((message.payload && message.payload.room) || '').toLowerCase();
  var now = cached();
  if (message.isSubscription && now) message.respond({ returnValue: true, json: now, done: false, stored: !!STORE });
  sync(room, function (json) {
    message.respond({ returnValue: true, json: json || now || '{"themes":[]}', done: true, stored: !!STORE });
  });
});

/* The menu can't read files in the service's own folder (file:// outside the app is blocked), so it asks here,
   a piece at a time: { name: "<id>.wav", offset } -> { data: base64, total } */
var READ_CHUNK = 384 * 1024;
service.register('read', function (message) {
  var p = message.payload || {}, name = String(p.name || ''), offset = Math.max(0, p.offset | 0);
  if (!STORE || !/^[a-z0-9][a-z0-9-]{0,40}\.(wav|ogg)$/.test(name)) { message.respond({ returnValue: false, errorText: 'not a sound file' }); return; }
  var file = path.join(STORE.dir, name);
  fs.stat(file, function (err, st) {
    if (err) { message.respond({ returnValue: false, errorText: 'not on this TV' }); return; }
    var len = Math.max(0, Math.min(READ_CHUNK, st.size - offset));
    if (!len) { message.respond({ returnValue: true, data: '', total: st.size }); return; }
    fs.open(file, 'r', function (e1, fd) {
      if (e1) { message.respond({ returnValue: false, errorText: 'cannot open' }); return; }
      var buf = Buffer.alloc ? Buffer.alloc(len) : new Buffer(len);
      fs.read(fd, buf, 0, len, offset, function (e2, n) {
        fs.close(fd, function () {});
        if (e2) { message.respond({ returnValue: false, errorText: 'cannot read' }); return; }
        message.respond({ returnValue: true, data: buf.slice(0, n).toString('base64'), total: st.size });
      });
    });
  });
});

service.register('cached', function (message) {
  message.respond({ returnValue: true, json: cached() || '{"themes":[]}', stored: !!STORE });
});
