package sg.round2.tvhome;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * Food order alerts for this room's TV.
 *
 * Make.com writes to this room's node in Firebase (FB_URL below) when an order is paid ("received") and when
 * Madd Pizza taps "Food is at the pickup point" in Telegram ("ready"). This service keeps one
 * connection to that topic open and shows the alert:
 *  - Round2 Home in front: the menu shows it in its own style (window.r2Food).
 *  - another app in front (Netflix, the PlayStation input): a card drawn over it, if Round2 Home
 *    may draw over other apps (round2tv.ps1 alerts switches that on); otherwise a plain toast.
 * A "ready" alert comes back once after 5 minutes, and again on the menu, until a guest presses OK.
 *
 * The topic name is r2-<FOOD_ALERT_KEY>-<room>. The key is a GitHub Actions secret, so it isn't
 * in the public code; without it the service does nothing. It reads only this room's topic.
 */
public class FoodAlerts extends Service {

    private static final String PREFS = "food";
    private static final long REPEAT_MS = 5 * 60_000;
    private static final long RECEIVED_SHOW_MS = 20_000, READY_SHOW_MS = 45_000;

    private static volatile HttpURLConnection live;
    private static volatile FoodAlerts inst;
    private final Handler main = new Handler(Looper.getMainLooper());
    private volatile boolean running;
    private View card;

    /* ---------- for the menu (Bridge) and the receivers ---------- */

    static boolean enabled() { return BuildConfig.FOOD_ALERT_KEY != null && BuildConfig.FOOD_ALERT_KEY.length() >= 12; }

    static void start(Context c) {
        if (!enabled()) return;
        try {
            Intent i = new Intent(c, FoodAlerts.class);
            if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i);
        } catch (Exception ignored) { }
    }

    /** The menu tells us the room (topic name), and its colours for the card. */
    static void setRoom(Context c, String room, String color, String ink) {
        if (room == null || !room.matches("[a-z]{2,20}")) return;
        SharedPreferences p = prefs(c);
        boolean changed = !room.equals(p.getString("room", ""));
        p.edit().putString("room", room).putString("color", color == null ? "" : color).putString("ink", ink == null ? "" : ink).apply();
        if (changed) { drop(); }
        start(c);
    }

    /** The Round2 menu came to the front: drop the card drawn over other apps (the menu shows its own). */
    static void menuInFront() {
        final FoodAlerts s = inst;
        if (s != null) s.main.post(new Runnable() { @Override public void run() { s.hideCard(); } });
    }

    /** A guest pressed OK on a "ready" alert: stop reminding. */
    static void seen(Context c, String no) {
        SharedPreferences p = prefs(c);
        if (no != null && no.equals(p.getString("pendingNo", ""))) p.edit().remove("pending").remove("pendingNo").apply();
        sendAcks(c);
    }

    /* ---------- "the room has seen it": a reply under the order in the Telegram group ---------- */
    static final String ACK_URL = "https://hook.eu1.make.com/ks522ziapwremxa35kdtan6h6l1oxe17";

    /** Remembers a "ready" order the guests haven't pressed OK on yet (its order reference). */
    private static void addUnacked(Context c, String ref) {
        if (ref == null || !ref.startsWith("R2-")) return;
        try {
            SharedPreferences p = prefs(c);
            org.json.JSONArray a = new org.json.JSONArray(p.getString("unacked", "[]")), out = new org.json.JSONArray();
            long now = System.currentTimeMillis();
            for (int i = 0; i < a.length(); i++) {
                JSONObject o = a.optJSONObject(i);
                if (o != null && !ref.equals(o.optString("ref")) && now - o.optLong("at") < 3 * 3600_000L) out.put(o);
            }
            out.put(new JSONObject().put("ref", ref).put("at", now));
            while (out.length() > 6) out.remove(0);
            p.edit().putString("unacked", out.toString()).apply();
        } catch (Exception ignored) { }
    }

    /** OK pressed on a "ready" alert: tells Make.com about every order waiting at the pickup point for this room
     *  (a room with two orders gets both confirmed). Make.com replies under each order's message in Telegram. */
    private static void sendAcks(Context c) {
        final SharedPreferences p = prefs(c);
        final String list = p.getString("unacked", "[]"), room = p.getString("room", "");
        p.edit().putString("unacked", "[]").apply();
        if (!enabled() || "[]".equals(list)) return;
        new Thread(new Runnable() { @Override public void run() {
            try {
                org.json.JSONArray a = new org.json.JSONArray(list);
                for (int i = 0; i < a.length(); i++) {
                    String ref = a.getJSONObject(i).optString("ref");
                    String body = "ref=" + java.net.URLEncoder.encode(ref, "UTF-8") + "&room=" + java.net.URLEncoder.encode(room, "UTF-8")
                            + "&k=" + java.net.URLEncoder.encode(BuildConfig.FOOD_ALERT_KEY, "UTF-8");
                    for (int tryNo = 0; tryNo < 2; tryNo++) {
                        HttpURLConnection h = null;
                        try {
                            h = (HttpURLConnection) new URL(ACK_URL).openConnection();
                            h.setConnectTimeout(15_000); h.setReadTimeout(15_000);
                            h.setDoOutput(true); h.setRequestMethod("POST");
                            h.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
                            h.getOutputStream().write(body.getBytes("UTF-8"));
                            if (h.getResponseCode() < 500) break;
                        } catch (Exception e) { sleep(3000); }
                        finally { if (h != null) h.disconnect(); }
                    }
                }
            } catch (Exception ignored) { }
        } }, "food-ack").start();
    }

    /** The "ready" alert nobody has confirmed yet (JSON), or null. */
    static String pending(Context c) {
        SharedPreferences p = prefs(c);
        String j = p.getString("pending", null);
        if (j == null) return null;
        if (System.currentTimeMillis() - p.getLong("pendingAt", 0) > 2 * 3600_000L) { p.edit().remove("pending").remove("pendingNo").apply(); return null; }
        return j;
    }

    private static SharedPreferences prefs(Context c) { return c.getSharedPreferences(PREFS, MODE_PRIVATE); }
    private static void drop() { HttpURLConnection c = live; if (c != null) try { c.disconnect(); } catch (Exception ignored) { } }

    /* ---------- the service ---------- */

    @Override public IBinder onBind(Intent i) { return null; }

    @Override
    public void onCreate() {
        super.onCreate();
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            NotificationChannel ch = new NotificationChannel("food", "Round2 food alerts", NotificationManager.IMPORTANCE_MIN);
            ch.setShowBadge(false);
            if (nm != null) nm.createNotificationChannel(ch);
            b = new Notification.Builder(this, "food");
        } else {
            b = new Notification.Builder(this).setPriority(Notification.PRIORITY_MIN);
        }
        b.setSmallIcon(R.drawable.icon).setContentTitle("Round2 food alerts").setContentText("Shows when your order is ready").setOngoing(true);
        try { startForeground(4403, b.build()); } catch (Exception ignored) { }
        if (!enabled()) { stopSelf(); return; }
        inst = this;
        running = true;
        new Thread(this::listen, "food-alerts").start();
    }

    @Override public int onStartCommand(Intent i, int flags, int id) { return START_STICKY; }

    @Override
    public void onDestroy() { if (inst == this) inst = null; running = false; drop(); main.removeCallbacksAndMessages(null); hideCard(); super.onDestroy(); }

    /** The Firebase Realtime Database that carries the alerts. Make.com writes each alert to
     *  /r2/<key>/<room>/last; every TV streams its own room's node (Server-Sent Events). */
    static final String FB_URL = "https://round2-tv-default-rtdb.asia-southeast1.firebasedatabase.app";

    private void listen() {
        long wait = 5_000;
        while (running) {
            String room = prefs(this).getString("room", "");
            if (room.isEmpty() || !FB_URL.startsWith("https://")) { sleep(20_000); continue; }
            HttpURLConnection c = null;
            try {
                URL u = new URL(FB_URL + "/r2/" + BuildConfig.FOOD_ALERT_KEY + "/" + room + "/last.json");
                c = (HttpURLConnection) u.openConnection();
                c.setConnectTimeout(15_000);
                c.setReadTimeout(120_000);   // Firebase sends a keep-alive every 30 s
                c.setRequestProperty("Accept", "text/event-stream");
                c.setRequestProperty("User-Agent", "Round2Home-Android");
                c.setInstanceFollowRedirects(true);
                live = c;
                BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream(), "UTF-8"));
                wait = 5_000;
                String line, event = "";
                while (running && (line = r.readLine()) != null) {
                    if (!room.equals(prefs(this).getString("room", ""))) break;   // room changed: reconnect
                    if (line.startsWith("event:")) event = line.substring(6).trim();
                    else if (line.startsWith("data:") && "put".equals(event)) handleEvent(line.substring(5).trim());
                    else if (line.startsWith("data:") && ("cancel".equals(event) || "auth_revoked".equals(event))) break;
                }
            } catch (Exception ignored) {
            } finally {
                live = null;
                if (c != null) try { c.disconnect(); } catch (Exception ignored) { }
            }
            if (running) { sleep(wait); wait = Math.min(wait * 2, 60_000); }
        }
    }

    /** One Firebase "put": {"path":"/","data":{"type":"ready","no":"231025","items":"...","ref":"R2-...","t":1793...}}.
     *  On every (re)connect Firebase sends the current value first; "t" (server time) tells new from already seen. */
    private void handleEvent(String raw) {
        try {
            JSONObject ev = new JSONObject(raw);
            if (!"/".equals(ev.optString("path"))) return;
            JSONObject d = ev.optJSONObject("data");
            if (d == null) return;
            long t = d.optLong("t", 0);
            if (t <= prefs(this).getLong("since", 0)) return;                // already shown
            prefs(this).edit().putLong("since", t).apply();
            String type = d.optString("type");
            long age = System.currentTimeMillis() / 1000 - t / 1000;
            if ("received".equals(type) && age > 180) return;            // old news after a reconnect
            if ("ready".equals(type) && age > 3600) return;
            if (!"received".equals(type) && !"ready".equals(type)) return;
            String items = d.optString("items");
            JSONObject p = new JSONObject().put("type", type).put("no", d.optString("no"))
                    .put("items", items.length() > 300 ? items.substring(0, 300) : items);
            if (d.optString("ref").startsWith("R2-")) p.put("ref", d.optString("ref"));
            final String json = p.toString();
            if ("ready".equals(type)) {
                addUnacked(this, p.optString("ref", null));
                prefs(this).edit().putString("pending", json).putString("pendingNo", p.optString("no")).putLong("pendingAt", System.currentTimeMillis()).apply();
                final String no = p.optString("no");
                main.postDelayed(new Runnable() { @Override public void run() {
                    String still = pending(FoodAlerts.this);
                    if (still != null && no.equals(prefs(FoodAlerts.this).getString("pendingNo", ""))) show(still, true);
                } }, REPEAT_MS);
            }
            main.post(new Runnable() { @Override public void run() { show(json, false); } });
        } catch (Exception ignored) { }
    }

    /* ---------- showing it ---------- */

    private void show(String json, boolean reminder) {
        if (MainActivity.showFood(json)) return;                         // Round2 Home is in front: the menu shows it
        JSONObject p;
        try { p = new JSONObject(json); } catch (Exception e) { return; }
        boolean ready = "ready".equals(p.optString("type"));
        String tag = "ORDER #" + p.optString("no") + (reminder ? " \u00B7 REMINDER" : "");
        String title = ready ? "LOOT DROPPED" : "ORDER LOCKED IN";
        String text = ready ? "It\u2019s at the drop zone \u2014 the rack right outside the entrance. Grab it while it\u2019s hot!"
                            : "Madd Pizza is firing it up. We\u2019ll ping you here when it lands.";
        if (Build.VERSION.SDK_INT >= 23 && !Settings.canDrawOverlays(this)) {
            try { Toast.makeText(this, title + "\n" + text, Toast.LENGTH_LONG).show(); } catch (Exception ignored) { }
            return;
        }
        showCard(tag, title, text, p.optString("items"), ready ? READY_SHOW_MS : RECEIVED_SHOW_MS, p.optString("no"), ready);
    }

    private int dp(float v) { return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()); }

    private int colour(String hex, int fallback) { try { return Color.parseColor(hex); } catch (Exception e) { return fallback; } }

    /** A card in the menu's style, top right, over whatever is playing. It can't take remote presses. */
    /** Sizes in the menu's 1920-wide units, so the card looks the same on every TV whatever its density. */
    private float u = 1f;
    private TextView okLabel;
    private int px(float v) { return Math.round(v * u); }

    /** A card in the menu's style, top right, over whatever is playing: a room-coloured "!" that pops
     *  and wobbles, and a card that flashes in the room colour.
     *  While it is up it takes the remote: OK or Back closes it (on a "ready" card that counts as seen);
     *  any other button just closes it (a "ready" card comes back once after 5 minutes).
     *  Netflix gets no presses until it is closed, so a guest can't skip past it by accident. */
    private void showCard(String tag, String title, String text, String items, long ms, final String no, final boolean ready) {
        hideCard();
        u = getResources().getDisplayMetrics().widthPixels / 1920f;
        final int room = colour(prefs(this).getString("color", ""), 0xFFF2C21B);
        final int ink = colour(prefs(this).getString("ink", ""), 0xFF0B0914);
        final int panel = 0xFF110E1A;

        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.HORIZONTAL);
        box.setClipChildren(false); box.setClipToPadding(false);
        box.setPadding(px(14), px(14), px(14), px(14));

        final TextView bang = new TextView(this);
        bang.setText("!"); bang.setGravity(Gravity.CENTER);
        bang.setTextColor(ink); bang.setBackgroundColor(room);
        bang.setTextSize(TypedValue.COMPLEX_UNIT_PX, px(44));
        bang.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(px(76), px(76));
        bl.rightMargin = px(10);
        box.addView(bang, bl);

        final LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(px(20), px(16), px(22), px(18));
        final android.graphics.drawable.GradientDrawable bgd = new android.graphics.drawable.GradientDrawable();
        bgd.setColor(panel); bgd.setStroke(Math.max(1, px(2)), room);
        col.setBackground(bgd);
        TextView t1 = new TextView(this);
        t1.setText(tag); t1.setTextColor(room); t1.setTextSize(TypedValue.COMPLEX_UNIT_PX, px(13));
        t1.setTypeface(Typeface.MONOSPACE, Typeface.BOLD); t1.setLetterSpacing(0.14f);
        TextView t2 = new TextView(this);
        t2.setText(title); t2.setTextColor(0xFFF4F1FB); t2.setTextSize(TypedValue.COMPLEX_UNIT_PX, px(34));
        t2.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD)); t2.setLetterSpacing(0.05f);
        t2.setPadding(0, px(8), 0, px(4));
        TextView t3 = new TextView(this);
        t3.setText(text); t3.setTextColor(0xFFD6D0E3); t3.setTextSize(TypedValue.COMPLEX_UNIT_PX, px(19));
        t3.setLineSpacing(0, 1.15f);
        col.addView(t1); col.addView(t2); col.addView(t3);
        // the order, one row per item, quantity in the room colour (at most 4 rows)
        java.util.List<String> list = new java.util.ArrayList<>();
        for (String x : (items == null ? "" : items).split("\\s*\u00B7\\s*|\n")) if (x.trim().length() > 0 && !"triggered".equals(x.trim())) list.add(x.trim());
        if (!list.isEmpty()) {
            View rule = new View(this); rule.setBackgroundColor(0x24F4F1FB);
            LinearLayout.LayoutParams rl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Math.max(1, px(1)));
            rl.topMargin = px(10); rl.bottomMargin = px(6);
            col.addView(rule, rl);
            for (int i = 0; i < list.size() && i < 5; i++) {
                String x = (i == 4 && list.size() > 5) ? "+" + (list.size() - 4) + " more" : list.get(i);
                java.util.regex.Matcher mm = java.util.regex.Pattern.compile("^(\\d+)\\s*x\\s+(.*)$", java.util.regex.Pattern.CASE_INSENSITIVE).matcher(x);
                android.text.SpannableStringBuilder sb = new android.text.SpannableStringBuilder();
                if (mm.matches()) {
                    sb.append(mm.group(1) + "\u00D7  ");
                    sb.setSpan(new android.text.style.ForegroundColorSpan(room), 0, sb.length(), 0);
                    sb.append(mm.group(2));
                } else sb.append(x);
                TextView li = new TextView(this);
                li.setText(sb); li.setTextColor(0xFFF4F1FB); li.setTextSize(TypedValue.COMPLEX_UNIT_PX, px(18));
                li.setTypeface(Typeface.DEFAULT_BOLD); li.setSingleLine(true); li.setEllipsize(android.text.TextUtils.TruncateAt.END);
                col.addView(li);
                if (i == 4) break;
            }
        }
        {
            final TextView ok = new TextView(this);
            okLabel = ok;
            ok.setText("[OK]  GOT IT"); ok.setTextColor(ink); ok.setBackgroundColor(room);
            ok.setTextSize(TypedValue.COMPLEX_UNIT_PX, px(15)); ok.setTypeface(Typeface.DEFAULT_BOLD); ok.setLetterSpacing(0.12f);
            ok.setPadding(px(12), px(7), px(22), px(7));
            LinearLayout.LayoutParams ol = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            ol.topMargin = px(12);
            col.addView(ok, ol);
        }
        box.addView(col, new LinearLayout.LayoutParams(px(410), LinearLayout.LayoutParams.WRAP_CONTENT));

        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
                Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY : WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.TOP | Gravity.END;
        lp.x = px(70); lp.y = px(56);
        try {
            WindowManager wm = (WindowManager) getSystemService(WINDOW_SERVICE);
            {
                box.setFocusable(true); box.setFocusableInTouchMode(true);
                box.setOnKeyListener(new View.OnKeyListener() {
                    @Override public boolean onKey(View v, int code, android.view.KeyEvent e) {
                        if (e.getAction() != android.view.KeyEvent.ACTION_UP) return true;   // swallow the press
                        if (code == android.view.KeyEvent.KEYCODE_DPAD_CENTER || code == android.view.KeyEvent.KEYCODE_ENTER
                                || code == android.view.KeyEvent.KEYCODE_BACK || code == android.view.KeyEvent.KEYCODE_NUMPAD_ENTER) {
                            if (ready) seen(FoodAlerts.this, no);
                            hideCard();
                        } else nudge(v, room, ink);   // any other button: shake the card and point at OK
                        return true;
                    }
                });
            }
            wm.addView(box, lp);
            box.requestFocus();
            final View me = box;
            card = box;
            // slide in, the "!" slams in and wobbles, the card flashes the room colour 4 times
            box.setAlpha(0f); box.setTranslationX(px(160));
            box.animate().alpha(1f).translationX(0).setDuration(560).setInterpolator(new android.view.animation.DecelerateInterpolator(2.2f)).start();
            playConfirm();
            bang.setScaleX(0f); bang.setScaleY(0f); bang.setRotation(-30f);
            bang.animate().scaleX(1f).scaleY(1f).rotation(0f).setStartDelay(100).setDuration(450)
                    .setInterpolator(new android.view.animation.OvershootInterpolator(3f)).start();
            android.animation.ObjectAnimator wob = android.animation.ObjectAnimator.ofFloat(bang, "rotation", 0f, -14f, 12f, -8f, 4f, 0f);
            wob.setDuration(700); wob.setStartDelay(700); wob.setRepeatCount(2); wob.start();
            for (int i = 0; i < 8; i++) {
                final boolean on = i % 2 == 0;
                main.postDelayed(new Runnable() { @Override public void run() { if (card == me) bgd.setColor(on ? room : panel); } }, 200 + i * 250L);
            }
            // then the "!" keeps blinking while the card is up
            main.postDelayed(new Runnable() {
                boolean lit;
                @Override public void run() {
                    if (card != me) return;
                    lit = !lit;
                    bang.setBackgroundColor(lit ? 0xFFF4F1FB : room); bang.setTextColor(lit ? 0xFF0B0914 : ink);
                    main.postDelayed(this, 550);
                }
            }, 3000);
            main.postDelayed(new Runnable() { @Override public void run() { if (card == me) hideCard(); } }, ms);
        } catch (Exception e) {
            try { Toast.makeText(this, title + "\n" + text, Toast.LENGTH_LONG).show(); } catch (Exception ignored) { }
        }
    }

    /** A button other than OK/Back: the card shakes and the OK label flashes, so first-timers see what to press. */
    private void nudge(View v, int room, int ink) {
        v.animate().cancel(); v.setTranslationX(0);
        android.animation.ObjectAnimator sh = android.animation.ObjectAnimator.ofFloat(v, "translationX", 0, -px(16), px(14), -px(10), px(6), 0);
        sh.setDuration(420); sh.start();
        final TextView ok = okLabel;
        if (ok == null) return;
        ok.setBackgroundColor(0xFFF4F1FB); ok.setTextColor(0xFF0B0914);
        ok.animate().scaleX(1.12f).scaleY(1.12f).setDuration(140).withEndAction(new Runnable() { @Override public void run() {
            ok.animate().scaleX(1f).scaleY(1f).setDuration(200).start();
        } }).start();
        final int r = room, i = ink;
        main.postDelayed(new Runnable() { @Override public void run() { ok.setBackgroundColor(r); ok.setTextColor(i); } }, 450);
    }

    /** The menu's own confirm sound. The menu renders its sounds to sfx-<pack>.ok.wav when it loads;
     *  the newest one is the pack in use. No file (sound off, or the menu hasn't run yet): silent. */
    private void playConfirm() {
        try {
            java.io.File best = null;
            java.io.File[] fs = getCacheDir().listFiles();
            if (fs != null) for (java.io.File f : fs)
                if (f.getName().startsWith("sfx-") && f.getName().endsWith(".ok.wav") && (best == null || f.lastModified() > best.lastModified())) best = f;
            if (best == null) return;
            final android.media.MediaPlayer mp = new android.media.MediaPlayer();
            mp.setAudioAttributes(new android.media.AudioAttributes.Builder()
                    .setUsage(android.media.AudioAttributes.USAGE_NOTIFICATION_EVENT)
                    .setContentType(android.media.AudioAttributes.CONTENT_TYPE_SONIFICATION).build());
            mp.setDataSource(best.getPath());
            mp.setOnCompletionListener(new android.media.MediaPlayer.OnCompletionListener() { @Override public void onCompletion(android.media.MediaPlayer m) { m.release(); } });
            mp.prepare();
            mp.start();
        } catch (Exception ignored) { }
    }

    private void hideCard() {
        okLabel = null;
        if (card == null) return;
        try { ((WindowManager) getSystemService(WINDOW_SERVICE)).removeView(card); } catch (Exception ignored) { }
        card = null;
    }

    private static void sleep(long ms) { try { Thread.sleep(ms); } catch (InterruptedException ignored) { } }
}
