/* Round2 control centre (staff.round2.sg) background worker.
   - Lets the page be installed as an app (always uses the network, nothing cached).
   - Shows push alerts sent by the Cloudflare engine (round2-book, /push/*): { title, body, tag, url, kind }.
   - Tapping an alert opens the control centre on that page (url like "/?tab=insession"). docs/staff-alerts.md */
self.addEventListener('install', function () { self.skipWaiting(); });
self.addEventListener('activate', function (e) { e.waitUntil(self.clients.claim()); });
self.addEventListener('fetch', function () { });

self.addEventListener('push', function (e) {
  var d = {};
  try { d = e.data ? e.data.json() : {}; } catch (x) { d = { title: 'Round2', body: e.data ? e.data.text() : '' }; }
  /* iPhones don't replace an alert with the same tag by themselves: when Apple held an alert back and the engine's
     re-send (5 min later) arrived with it, both showed (Suaidah, 11 Nov). Close the older one first, so one stays. */
  var clear = d.tag && self.registration.getNotifications ? self.registration.getNotifications({ tag: d.tag }).then(function (L) { L.forEach(function (n) { n.close(); }); }).catch(function () {}) : Promise.resolve();
  var shown = clear.then(function () { return self.registration.showNotification(d.title || 'Round2', {
    body: d.body || '',
    tag: d.tag || undefined,          /* the same booking replaces its older alert */
    renotify: !!d.tag,
    icon: 'icon-192.png',
    badge: 'icon-192.png',
    data: { url: d.url || '/' }
  }); });
  /* tell the engine this phone really showed it (owner, 6 Nov: an alert Apple accepted never appeared on his phone) –
     the control centre can then say which phones got which alert */
  var id = /[?&]alert=([a-z0-9]+)/.exec(d.url || '');
  var told = !id ? Promise.resolve() : shown.then(function () { return self.registration.pushManager.getSubscription(); }).then(function (sub) {
    if (!sub) return;
    return fetch('https://round2-book.donttelldadyet.workers.dev/push/seen', { method: 'POST', body: JSON.stringify({ a: id[1], e: sub.endpoint.slice(-40) }) });
  }).catch(function () {});
  e.waitUntil(Promise.all([shown, told]));
});

self.addEventListener('notificationclick', function (e) {
  e.notification.close();
  var url = new URL((e.notification.data && e.notification.data.url) || '/', self.location.origin).href;
  e.waitUntil(self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then(function (list) {
    for (var i = 0; i < list.length; i++) {
      var w = list[i];
      if (new URL(w.url).origin === self.location.origin && 'focus' in w) {
        w.postMessage({ r2open: url });
        return w.focus();
      }
    }
    return self.clients.openWindow(url);
  }));
});
