/* Round2 control centre (staff.round2.sg) background worker.
   - Lets the page be installed as an app (always uses the network, nothing cached).
   - Shows push alerts sent by the Cloudflare engine (round2-book, /push/*): { title, body, tag, url, kind }.
   - Tapping an alert opens the control centre on that page (url like "/?tab=insession"). docs/staff-alerts.md */
self.addEventListener('install', function () { self.skipWaiting(); });
self.addEventListener('activate', function (e) { e.waitUntil(self.clients.claim()); });
self.addEventListener('fetch', function () { });

/* Version of this worker, told to the engine with every "shown" report. 2 = understands declarative alerts (below);
   the engine only sends those to a phone whose worker has reported 2 or more. */
var SWV = 2;

/* An alert can reach us in three shapes (14 Nov):
   1. the old one – our own { title, body, tag, url, kind };
   2. declarative, on a phone that understands it (iOS 18.4+): the phone hands us the alert it is about to show
      (e.notification, no data). We show it ourselves so we can report "shown"; if this worker can't run, the phone shows
      it by itself – that is the point (docs/staff-alerts.md "Declarative Web Push");
   3. declarative, on a browser that doesn't know it: it arrives as data { web_push: 8030, notification: {…} }. */
function r2Alert(e) {
  var d = {};
  try { d = e.data ? e.data.json() : {}; } catch (x) { d = { title: 'Round2', body: e.data ? e.data.text() : '' }; }
  var n = null;
  if (d && d.web_push === 8030 && d.notification) n = d.notification;
  else if (!e.data && e.notification) n = e.notification;
  if (n) { var nd = n.data || {}; d = { title: n.title, body: n.body, tag: n.tag, url: nd.url || n.navigate || '/', kind: nd.kind }; }
  return d || {};
}

self.addEventListener('push', function (e) {
  var d = r2Alert(e);
  /* iPhones don't replace an alert with the same tag by themselves (WebKit bug 258922). Closing the older one first helps
     when the copies come apart; it can't when they arrive in the same second (the phone's list is stale and it won't close
     an alert in its first 30 s) – the control centre tidies those up when it is opened (index.html, r2TidyAlerts). */
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
     the control centre can then say which phones got which alert. A test alert has no page of its own: reported as
     "test", which still counts as a sign of life for this phone (the engine drops sign-ups silent for a week). */
  var id = /[?&]alert=([a-z0-9]+)/.exec(d.url || '');
  var told = shown.then(function () { return self.registration.pushManager.getSubscription(); }).then(function (sub) {
    if (!sub) return;
    return fetch('https://round2-book.donttelldadyet.workers.dev/push/seen', { method: 'POST', body: JSON.stringify({ a: id ? id[1] : 'test', e: sub.endpoint.slice(-40), v: SWV }) });
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
