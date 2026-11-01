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
  e.waitUntil(self.registration.showNotification(d.title || 'Round2', {
    body: d.body || '',
    tag: d.tag || undefined,          /* the same booking replaces its older alert */
    renotify: !!d.tag,
    icon: 'icon-192.png',
    badge: 'icon-192.png',
    data: { url: d.url || '/' }
  }));
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
