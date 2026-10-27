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
 * Make.com posts to this room's private ntfy.sh topic when an order is paid ("received") and when
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
    private static final long RECEIVED_SHOW_MS = 12_000, READY_SHOW_MS = 45_000;

    private static volatile HttpURLConnection live;
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
        if (changed) { p.edit().putLong("since", 0).apply(); drop(); }
        start(c);
    }

    /** A guest pressed OK on a "ready" alert: stop reminding. */
    static void seen(Context c, String no) {
        SharedPreferences p = prefs(c);
        if (no != null && no.equals(p.getString("pendingNo", ""))) p.edit().remove("pending").remove("pendingNo").apply();
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
        running = true;
        new Thread(this::listen, "food-alerts").start();
    }

    @Override public int onStartCommand(Intent i, int flags, int id) { return START_STICKY; }

    @Override
    public void onDestroy() { running = false; drop(); main.removeCallbacksAndMessages(null); hideCard(); super.onDestroy(); }

    private void listen() {
        long wait = 5_000;
        while (running) {
            String room = prefs(this).getString("room", "");
            if (room.isEmpty()) { sleep(20_000); continue; }
            long since = prefs(this).getLong("since", 0);
            HttpURLConnection c = null;
            try {
                String topic = "r2-" + BuildConfig.FOOD_ALERT_KEY + "-" + room;
                URL u = new URL("https://ntfy.sh/" + topic + "/json?since=" + (since > 0 ? String.valueOf(since + 1) : "10m"));
                c = (HttpURLConnection) u.openConnection();
                c.setConnectTimeout(15_000);
                c.setReadTimeout(120_000);   // ntfy sends a keepalive about every 45 s
                c.setRequestProperty("User-Agent", "Round2Home-Android");
                live = c;
                BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream(), "UTF-8"));
                wait = 5_000;
                String line;
                while (running && (line = r.readLine()) != null) {
                    if (!room.equals(prefs(this).getString("room", ""))) break;   // room changed: reconnect
                    handleLine(line);
                }
            } catch (Exception ignored) {
            } finally {
                live = null;
                if (c != null) try { c.disconnect(); } catch (Exception ignored) { }
            }
            if (running) { sleep(wait); wait = Math.min(wait * 2, 60_000); }
        }
    }

    private void handleLine(String line) {
        try {
            JSONObject m = new JSONObject(line);
            if (!"message".equals(m.optString("event"))) return;
            long time = m.optLong("time", 0);
            prefs(this).edit().putLong("since", time).apply();
            // Make.com sends Title "received 231025" / "ready 231025" and the items as the message
            String[] tn = m.optString("title").trim().split(" ", 2);
            JSONObject p = new JSONObject().put("type", tn[0]).put("no", tn.length > 1 ? tn[1] : "")
                    .put("items", m.optString("message").length() > 300 ? m.optString("message").substring(0, 300) : m.optString("message"));
            String type = p.optString("type");
            long age = System.currentTimeMillis() / 1000 - time;
            if ("received".equals(type) && age > 180) return;            // old news after a reconnect
            if ("ready".equals(type) && age > 3600) return;
            if (!"received".equals(type) && !"ready".equals(type)) return;
            final String json = p.toString();
            if ("ready".equals(type)) {
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
        showCard(tag, title, text, ready ? READY_SHOW_MS : RECEIVED_SHOW_MS);
    }

    private int dp(float v) { return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()); }

    private int colour(String hex, int fallback) { try { return Color.parseColor(hex); } catch (Exception e) { return fallback; } }

    /** A card in the menu's style, top right, over whatever is playing. It can't take remote presses. */
    /** Sizes in the menu's 1920-wide units, so the card looks the same on every TV whatever its density. */
    private float u = 1f;
    private int px(float v) { return Math.round(v * u); }

    /** A card in the menu's style, top right, over whatever is playing: a room-coloured "!" that pops
     *  and wobbles, and a card that flashes in the room colour. It can't take remote presses. */
    private void showCard(String tag, String title, String text, long ms) {
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
        box.addView(col, new LinearLayout.LayoutParams(px(410), LinearLayout.LayoutParams.WRAP_CONTENT));

        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
                Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY : WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.TOP | Gravity.END;
        lp.x = px(70); lp.y = px(56);
        try {
            WindowManager wm = (WindowManager) getSystemService(WINDOW_SERVICE);
            wm.addView(box, lp);
            final View me = box;
            card = box;
            // slide in, the "!" slams in and wobbles, the card flashes the room colour 4 times
            box.setAlpha(0f); box.setTranslationX(px(120));
            box.animate().alpha(1f).translationX(0).setDuration(380).setInterpolator(new android.view.animation.OvershootInterpolator(1.2f)).start();
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

    private void hideCard() {
        if (card == null) return;
        try { ((WindowManager) getSystemService(WINDOW_SERVICE)).removeView(card); } catch (Exception ignored) { }
        card = null;
    }

    private static void sleep(long ms) { try { Thread.sleep(ms); } catch (InterruptedException ignored) { } }
}
