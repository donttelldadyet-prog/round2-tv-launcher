package sg.round2.tvhome;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.util.HashMap;
import java.util.Map;
import android.media.tv.TvContract;
import android.media.tv.TvInputInfo;
import android.media.tv.TvInputManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Round2 Home: a TV home screen that shows the Round2 menu (assets/index.html)
 * and lets it open apps and HDMI inputs through the R2Android bridge.
 */
public class MainActivity extends Activity {

    private WebView web;
    private NativeBackground bg;
    private ThemeSync themes;
    private Updater updater;
    NativeSound sound;

    /* Watchdog: if remote presses stop reaching the menu (a stuck web page), reload it by itself */
    private volatile long lastAck = System.currentTimeMillis();
    private android.widget.TextView splash;
    private volatile int unacked = 0;
    private long firstUnacked = 0, lastReload = 0;
    private String pendingRoom;
    private boolean pageReady;
    private static volatile MainActivity front;   // the menu while it is on screen (food alerts go to it)
    private long stoppedAt;   // when the screen was last hidden (an app, the screensaver, standby)
    private boolean screenWasOff;   // standby since the menu was last shown: rebuild the ambience players
    private static final long RELOAD_AFTER_MS = 10 * 60 * 1000L;

    /* TV switched off (standby): guests' changes are forgotten, the house settings come back */
    private final android.content.BroadcastReceiver screenOff = new android.content.BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) {
            screenWasOff = true;
            if (web != null) web.evaluateJavascript("window.r2Reset && window.r2Reset()", null);
            // Android 12+: a downloaded-ready update installs itself while the TV is off
            if (updater != null && updater.hasUpdate() && Updater.canInstallQuietly())
                updater.install(new Updater.Report() { @Override public void report(org.json.JSONObject o) { } }, true);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        web = new WebView(this);
        web.setBackgroundColor(0x00000000);   // transparent: the native background video shows through
        bg = new NativeBackground(this);
        themes = new ThemeSync(this);
        updater = new Updater(this);
        sound = new NativeSound(this);
        CrashLog.install(this);

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);   // muted background video
        s.setAllowFileAccess(false);
        s.setTextZoom(100);
        s.setUserAgentString(s.getUserAgentString() + " Round2Home/2");   // lets the menu know it runs in the app
        // No offscreen layer: the WebView draws straight into the window (one less full-screen copy per frame)
        web.setLayerType(View.LAYER_TYPE_NONE, null);
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        web.setVerticalScrollBarEnabled(false);
        web.setHorizontalScrollBarEnabled(false);                // ignore the TV's large-text setting; the menu scales itself
        s.setUseWideViewPort(false);       // lay out at the screen's own size; the menu scales itself
        s.setLoadWithOverviewMode(false);
        web.setInitialScale(0);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                return true;   // the home screen never navigates away
            }

            /* App artwork for the More apps tiles: https://r2.icon/<package> returns that app's TV banner (or icon) */
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest req) {
                Uri u = req.getUrl();
                /* Event background thumbnails: https://r2.theme/<id>.jpg from the downloaded themes */
                if (u != null && "r2.theme".equals(u.getHost())) {
                    String n = u.getLastPathSegment();
                    java.io.File f = n == null ? null : new java.io.File(themes.dir, n);
                    if (f != null && n.matches("[a-z0-9-]+\\.jpg") && f.exists()) {
                        try { return new WebResourceResponse("image/jpeg", null, new java.io.FileInputStream(f)); } catch (Exception ignored) { }
                    }
                    return new WebResourceResponse("text/plain", null, new ByteArrayInputStream(new byte[0]));
                }
                if (u != null && "r2.icon".equals(u.getHost())) {
                    byte[] png = artFor(u.getLastPathSegment());
                    if (png != null) return new WebResourceResponse("image/png", null, new ByteArrayInputStream(png));
                    return new WebResourceResponse("text/plain", null, new ByteArrayInputStream(new byte[0]));
                }
                return null;
            }

            /* If the TV reclaims the menu's renderer (e.g. while the screensaver runs), rebuild the
               screen instead of showing a half-drawn page */
            @Override
            public boolean onRenderProcessGone(WebView view, android.webkit.RenderProcessGoneDetail d) {
                recreate();
                return true;
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                pageReady = true;
                applyPendingRoom();
                if (pendingMsg != null) { final String j = pendingMsg; pendingMsg = null;
                    web.postDelayed(new Runnable() { @Override public void run() { if (web != null) web.evaluateJavascript("window.r2Msg && window.r2Msg(" + j + ")", null); } }, 1500); }
                if (splash != null) splash.postDelayed(new Runnable() { @Override public void run() {   // the menu's boot screen has drawn by now
                    if (splash != null) { splash.animate().alpha(0f).setDuration(250).start(); splash.postDelayed(new Runnable() {
                        @Override public void run() { if (splash != null) { splash.setVisibility(View.GONE); } } }, 300); }
                } }, 250);
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            /* the menu's messages go to the TV's log, so 'round2tv.ps1 diag' can collect them */
            @Override public boolean onConsoleMessage(android.webkit.ConsoleMessage m) {
                android.util.Log.i("R2Home", "menu: " + m.message() + " (" + m.sourceId() + ":" + m.lineNumber() + ")");
                return true;
            }
        });
        web.addJavascriptInterface(new Bridge(this), "R2Android");
        web.setFocusable(true);
        web.setFocusableInTouchMode(true);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xFF06050A);
        root.addView(bg.view, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(web, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        /* shown while the TV's web engine warms up, before the menu's own boot screen can draw */
        splash = new android.widget.TextView(this);
        splash.setText("ROUND2\n\nSTARTING UP\u2026");
        splash.setTextColor(0xFFB9B0CF);
        splash.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 18);
        splash.setLetterSpacing(0.3f);
        splash.setTypeface(android.graphics.Typeface.MONOSPACE, android.graphics.Typeface.BOLD);
        splash.setGravity(android.view.Gravity.CENTER);
        splash.setBackgroundColor(0xFF06050A);
        root.addView(splash, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setContentView(root);
        registerReceiver(screenOff, new android.content.IntentFilter(Intent.ACTION_SCREEN_OFF));
        pendingRoom = getIntent().getStringExtra("room");
        web.loadUrl("file:///android_asset/index.html");
        web.requestFocus(View.FOCUS_DOWN);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        // Set this TV's room from the computer:
        // adb shell am start -n sg.round2.tvhome/.MainActivity --es room vale
        String room = intent.getStringExtra("room");
        if (room != null) { pendingRoom = room; applyPendingRoom(); }
        // Check for new event backgrounds now: adb shell am start -n sg.round2.tvhome/.MainActivity --ez syncThemes true
        // The remote's Home button (via the Home button helper) while the menu is already open: back to the main menu
        if (intent.getBooleanExtra("home", false) && web != null && pageReady)
            web.evaluateJavascript("window.r2Home && window.r2Home()", null);
        if (intent.getBooleanExtra("syncThemes", false) && web != null)
            web.evaluateJavascript("window.r2SyncThemes && window.r2SyncThemes(true)", null);
    }

    private final Map<String, byte[]> artCache = new HashMap<>();

    /** The app's TV banner if it has one; true when it does. */
    Drawable bannerOf(String pkg) {
        PackageManager pm = getPackageManager();
        try {
            Intent i = pm.getLeanbackLaunchIntentForPackage(pkg);
            if (i != null) {
                ResolveInfo ri = pm.resolveActivity(i, 0);
                if (ri != null) { Drawable d = ri.activityInfo.loadBanner(pm); if (d != null) return d; }
            }
            return pm.getApplicationBanner(pkg);
        } catch (Exception e) { return null; }
    }

    byte[] artFor(String pkg) {
        if (pkg == null) return null;
        synchronized (artCache) { if (artCache.containsKey(pkg)) return artCache.get(pkg); }
        byte[] out = null;
        try {
            Drawable d = bannerOf(pkg);
            if (d == null) d = getPackageManager().getApplicationIcon(pkg);
            Bitmap bmp;
            if (d instanceof BitmapDrawable && ((BitmapDrawable) d).getBitmap() != null) bmp = ((BitmapDrawable) d).getBitmap();
            else {
                int w = d.getIntrinsicWidth() > 0 ? d.getIntrinsicWidth() : 320, h = d.getIntrinsicHeight() > 0 ? d.getIntrinsicHeight() : 180;
                bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                Canvas c = new Canvas(bmp); d.setBounds(0, 0, w, h); d.draw(c);
            }
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            bmp.compress(Bitmap.CompressFormat.PNG, 100, bo);
            out = bo.toByteArray();
        } catch (Exception ignored) { }
        synchronized (artCache) { artCache.put(pkg, out); }
        return out;
    }

    private void applyPendingRoom() {
        if (!pageReady || pendingRoom == null) return;
        String safe = pendingRoom.replaceAll("[^a-z]", "");
        pendingRoom = null;
        web.evaluateJavascript("window.r2SetRoom && window.r2SetRoom('" + safe + "')", null);
    }

    @Override
    protected void onResume() {
        super.onResume();
        lastAck = System.currentTimeMillis(); unacked = 0;
        HomeWatch.start(this);   // no-op unless Usage access is on (Android 9 TVs without the Accessibility helper)
        front = this;
        FoodAlerts.start(this);  // no-op in builds without the food alert key
        FoodAlerts.menuInFront();
        lastKeyAt = System.currentTimeMillis(); applyAwake();   // back on the menu: the TV's screensaver stays away again

        if (bg != null) bg.resume();
        long awayMs = stoppedAt > 0 ? System.currentTimeMillis() - stoppedAt : 0;
        // after standby or a long time away (screensaver), the TV may have re-routed its audio: fresh players
        if (sound != null && (screenWasOff || awayMs > 60_000)) sound.revive();
        screenWasOff = false;
        if (web != null) {
            web.onResume();
            web.requestFocus();
            long away = awayMs;
            stoppedAt = 0;
            if (away > RELOAD_AFTER_MS || !pageReady) {
                // Back after a long time (screensaver, standby, a long film): start the menu fresh
                pageReady = false;
                web.reload();
            } else {
                web.evaluateJavascript("window.r2Resume && window.r2Resume()", null);
            }
            web.invalidate();
            web.postDelayed(new Runnable() { @Override public void run() { if (web != null) web.invalidate(); } }, 400);
        }
    }

    @Override
    protected void onDestroy() {
        try { unregisterReceiver(screenOff); } catch (Exception ignored) { }
        if (sound != null) sound.release();
        super.onDestroy();
    }

    @Override
    protected void onStop() {
        stoppedAt = System.currentTimeMillis();
        super.onStop();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && web != null) {
            web.requestFocus(); web.invalidate();                                  // redraw after the screensaver or a popup
            web.evaluateJavascript("window.r2Focus && window.r2Focus()", null);   // back from an app that never paused us
        }
    }

    private String pendingMsg;   // a message that arrived while the menu was loading

    /* ---- the TV's own screensaver (Staff tools > TV screensaver): while the menu is on screen it keeps the screen on
       (FLAG_KEEP_SCREEN_ON, so Android's screensaver/Daydream and screen-off timer don't start). After 3 hours without a
       remote press on the menu it lets go, so a TV left on overnight still goes to its screensaver and sleeps. ---- */
    static final long AWAKE_IDLE_MS = 3 * 3600_000L;
    private long lastKeyAt = System.currentTimeMillis();
    private final Runnable awakeCheck = new Runnable() { @Override public void run() { applyAwake(); } };
    static boolean blockSaver(android.content.Context c) { return c.getSharedPreferences("r2home", Context.MODE_PRIVATE).getBoolean("blockSaver", true); }
    void applyAwake() {
        if (web == null) return;
        web.removeCallbacks(awakeCheck);
        long left = AWAKE_IDLE_MS - (System.currentTimeMillis() - lastKeyAt);
        if (blockSaver(this) && left > 0 && front == this) {
            getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            web.postDelayed(awakeCheck, left + 1000);
        } else getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    protected void onPause() {
        if (front == this) front = null;
        pendingMsg = null;
        FoodAlerts.menuLeft();   // a message waiting for OK follows the guest as a small banner
        if (bg != null) bg.pause();
        if (web != null) web.evaluateJavascript("window.r2Pause && window.r2Pause()", null);   // stop the rain ambience
        if (web != null) web.onPause();   // stops the background video while an app is open
        if (sound != null) { sound.loopTo(0f, 150); sound.fileTo(null, 0f, 150); }
        super.onPause();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (web != null && !web.hasFocus()) web.requestFocus();      // keys always go to the menu
        int k = e.getKeyCode();
        if (e.getAction() == KeyEvent.ACTION_DOWN) { boolean wasIdle = System.currentTimeMillis() - lastKeyAt > AWAKE_IDLE_MS; lastKeyAt = System.currentTimeMillis(); if (wasIdle) applyAwake(); }
        if (e.getAction() == KeyEvent.ACTION_DOWN && (k == KeyEvent.KEYCODE_DPAD_UP || k == KeyEvent.KEYCODE_DPAD_DOWN
                || k == KeyEvent.KEYCODE_DPAD_LEFT || k == KeyEvent.KEYCODE_DPAD_RIGHT || k == KeyEvent.KEYCODE_DPAD_CENTER)) noteKey();
        if (e.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (e.getAction() == KeyEvent.ACTION_UP) {
                web.evaluateJavascript("window.r2Back && window.r2Back()", null);
            }
            return true;   // a home screen never closes on Back
        }
        return super.dispatchKeyEvent(e);
    }

    /* A key went to the menu; the menu answers keyAck() when it handled it. Four unanswered presses over
       3 seconds means the page is stuck: reload it (and rebuild the screen if that keeps happening). */
    private void noteKey() {
        long now = System.currentTimeMillis();
        // counts only presses the menu hasn't answered yet (keyAck clears them); slow browsing never adds up
        if (unacked == 0) firstUnacked = now;
        unacked++;
        if (unacked >= 4 && now - firstUnacked > 3000 && pageReady) {
            android.util.Log.w("R2Home", "menu not answering keys: reloading");
            unacked = 0;
            if (now - lastReload < 120000) { recreate(); return; }
            lastReload = now; pageReady = false; lastAck = now;
            web.reload();
        }
    }

    /** Methods the web menu can call as window.R2Android.* */
    /** A food alert from FoodAlerts: the menu shows it if it is on screen. False = show it natively. */
    static boolean showFood(final String json) {
        final MainActivity a = front;
        if (a == null || a.web == null || !a.pageReady) return false;
        a.runOnUiThread(new Runnable() { @Override public void run() {
            if (a.web != null) a.web.evaluateJavascript("window.r2Food && window.r2Food(" + json + ")", null);
        } });
        return true;
    }

    /** A personal message (FoodAlerts): the menu shows it if it is on screen. False = show it natively. */
    static boolean showMsg(final String json) {
        final MainActivity a = front;
        if (a == null) return false;
        /* Round2 Home is in front but the menu is still (re)loading (after standby, a long film): hold the message and
           show it as the big banner once the menu is ready – never a small banner over our own home screen */
        if (a.web == null || !a.pageReady) { a.pendingMsg = json; return true; }
        a.runOnUiThread(new Runnable() { @Override public void run() {
            if (a.web != null) a.web.evaluateJavascript("window.r2Msg && window.r2Msg(" + json + ")", null);
        } });
        return true;
    }

    static final class Bridge {
        private final MainActivity act;

        /* ---- food order alerts (FoodAlerts) ---- */
        /** "1" if this build has the alert key, so the menu can leave the alerts to the app. */
        @JavascriptInterface
        public String foodAlerts() { return FoodAlerts.enabled() ? "1" : "0"; }

        @JavascriptInterface
        public void foodRoom(String room, String color, String ink) { FoodAlerts.setRoom(act, room, color, ink); }

        @JavascriptInterface
        public void foodSeen(String no) { FoodAlerts.seen(act, no); }

        /** Staff tools > TV screensaver: "1" = the menu keeps the TV's screensaver away (default). */
        @JavascriptInterface
        public String blockSaver() { return MainActivity.blockSaver(act) ? "1" : "0"; }

        @JavascriptInterface
        public void setBlockSaver(final boolean on) {
            act.getSharedPreferences("r2home", Context.MODE_PRIVATE).edit().putBoolean("blockSaver", on).apply();
            act.runOnUiThread(new Runnable() { @Override public void run() { act.applyAwake(); } });
        }

        /** A personal message due now and not yet acknowledged ("" if none). */
        @JavascriptInterface
        public String msgDue() { String j = FoodAlerts.msgDue(act); return j == null ? "" : j; }

        @JavascriptInterface
        public void msgAck(String id) { FoodAlerts.msgAck(act, id); }

        /** A "ready" alert nobody has pressed OK on yet ("" if none). */
        @JavascriptInterface
        public String foodPending() { String j = FoodAlerts.pending(act); return j == null ? "" : j; }

        Bridge(MainActivity act) { this.act = act; }

        /* ---- native background video ---- */
        @JavascriptInterface
        public String bgPlay(final String assetPath) {
            if (assetPath == null) return "no";
            final String path;
            if (assetPath.startsWith("media/")) path = assetPath;                     // bundled in the app
            else if (assetPath.matches("theme/[a-z0-9-]+\\.mp4")) {                    // downloaded event theme
                java.io.File f = new java.io.File(act.themes.dir, assetPath.substring(6));
                if (!f.exists()) return "no";
                path = f.getAbsolutePath();
            } else return "no";
            act.runOnUiThread(new Runnable() { @Override public void run() { act.bg.play(path); } });
            return "ok";
        }

        /* ---- About this TV (staff): its address on the shop network, for the PC toolkit ---- */
        @JavascriptInterface
        public String ip() {
            try {
                for (java.net.NetworkInterface ni : java.util.Collections.list(java.net.NetworkInterface.getNetworkInterfaces())) {
                    if (!ni.isUp() || ni.isLoopback()) continue;
                    for (java.net.InetAddress a : java.util.Collections.list(ni.getInetAddresses()))
                        if (a instanceof java.net.Inet4Address && a.isSiteLocalAddress()) return a.getHostAddress();
                }
            } catch (Exception ignored) { }
            return "";
        }

        /* ---- watchdog ---- */
        @JavascriptInterface
        public void keyAck() { act.lastAck = System.currentTimeMillis(); act.unacked = 0; }

        /* ---- sounds played by Android (see NativeSound) ---- */
        @JavascriptInterface
        public String audioNative() { return "1"; }

        @JavascriptInterface
        public void sfxLoad(String key, String b64) {
            if (key == null || b64 == null || !key.matches("[a-z0-9.]{1,40}")) return;
            try { act.sound.load(key, android.util.Base64.decode(b64, android.util.Base64.DEFAULT)); } catch (Exception ignored) { }
        }

        @JavascriptInterface
        public String sfxHas(String key) { return key != null && act.sound.has(key) ? "1" : "0"; }

        @JavascriptInterface
        public void sfxPlay(String key, double vol) { if (key != null) act.sound.play(key, (float) vol); }

        @JavascriptInterface
        public void loopLoad(String b64, int sampleRate) {
            if (b64 == null || sampleRate < 8000 || sampleRate > 48000) return;
            try { act.sound.loadLoop(android.util.Base64.decode(b64, android.util.Base64.DEFAULT), sampleRate); } catch (Exception ignored) { }
        }

        @JavascriptInterface
        public String loopHas() { return act.sound.hasLoop() ? "1" : "0"; }

        /** The menu's equaliser: n levels of the rain being played ("" when it isn't). */
        @JavascriptInterface
        public String ambLevels(int n) { return act.sound == null ? "" : act.sound.levels(n); }

        @JavascriptInterface
        public void loopTo(double vol, int ms) { act.sound.loopTo((float) vol, ms); }

        /** an uploaded ambience (theme/<id>.ogg, downloaded by ThemeSync); empty id = fade out the current one */
        @JavascriptInterface
        public void ambFile(String id, double vol, int ms) {
            String path = null;
            if (id != null && id.matches("[a-z0-9][a-z0-9-]{0,40}")) path = new java.io.File(act.themes.dir, id + ".ogg").getAbsolutePath();
            act.sound.fileTo(path, (float) vol, ms);
        }

        @JavascriptInterface
        public String version() {
            try { return act.getPackageManager().getPackageInfo(act.getPackageName(), 0).versionName; } catch (Exception e) { return ""; }
        }

        /* ---- Update now (staff): newest Round2 Home from GitHub ---- */
        private void sendUpdate(final org.json.JSONObject st) {
            act.runOnUiThread(new Runnable() { @Override public void run() {
                if (act.web != null) act.web.evaluateJavascript("window.r2Update && window.r2Update(" + st.toString() + ")", null);
            } });
        }

        @JavascriptInterface
        public void updateCheck() { act.updater.check(new Updater.Report() { @Override public void report(org.json.JSONObject o) { sendUpdate(o); } }); }

        @JavascriptInterface
        public void updateInstall() { act.updater.install(new Updater.Report() { @Override public void report(org.json.JSONObject o) { sendUpdate(o); } }); }

        /* ---- event backgrounds pushed from GitHub ---- */
        @JavascriptInterface
        public String themes() { return act.themes.cached(); }

        @JavascriptInterface
        public void themesSync(String room) {
            act.themes.sync(room, new ThemeSync.Done() {
                @Override public void done(final String json) {
                    act.runOnUiThread(new Runnable() { @Override public void run() {
                        if (act.web != null) act.web.evaluateJavascript("window.r2Themes && window.r2Themes(" + JSONObject.quote(json) + ")", null);
                    } });
                }
            });
        }

        @JavascriptInterface
        public void bgStop() {
            act.runOnUiThread(new Runnable() { @Override public void run() { act.bg.stop(); } });
        }

        @JavascriptInterface
        public void bgZoom(final float from, final float to, final int ms, final float px, final float py, final boolean easeIn) {
            act.runOnUiThread(new Runnable() { @Override public void run() { act.bg.zoom(from, to, ms, px, py, easeIn); } });
        }

        @JavascriptInterface
        public String launch(String pkg) {
            if (pkg == null || pkg.isEmpty()) return "No app is set for this item.";
            PackageManager pm = act.getPackageManager();
            Intent i = pm.getLeanbackLaunchIntentForPackage(pkg);
            if (i == null) i = pm.getLaunchIntentForPackage(pkg);
            if (i == null) return "This app isn't installed on this TV.";
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            try { act.startActivity(i); return "ok"; }
            catch (ActivityNotFoundException ex) { return "The TV could not open this app."; }
        }

        /** HDMI and other hardware inputs, as [{id, name}]. */
        @JavascriptInterface
        public String inputs() {
            JSONArray out = new JSONArray();
            if (Build.VERSION.SDK_INT < 21) return out.toString();
            try {
                TvInputManager tim = (TvInputManager) act.getSystemService(Context.TV_INPUT_SERVICE);
                if (tim == null) return out.toString();
                List<TvInputInfo> list = new ArrayList<>(tim.getTvInputList());
                List<JSONObject> rows = new ArrayList<>();
                for (TvInputInfo in : list) {
                    if (!in.isPassthroughInput()) continue;
                    if (in.getParentId() != null) continue;   // skip HDMI-CEC child devices
                    String name = String.valueOf(in.loadLabel(act));
                    JSONObject o = new JSONObject();
                    o.put("id", in.getId());
                    o.put("name", name);
                    o.put("type", in.getType());
                    rows.add(o);
                }
                Collections.sort(rows, new Comparator<JSONObject>() {
                    @Override public int compare(JSONObject a, JSONObject b) {
                        return a.optString("name").compareToIgnoreCase(b.optString("name"));
                    }
                });
                for (JSONObject o : rows) out.put(o);
            } catch (Exception ignored) { }
            return out.toString();
        }

        @JavascriptInterface
        public String openInput(String inputId) {
            if (Build.VERSION.SDK_INT < 21) return "This TV can't switch inputs from apps.";
            try {
                Uri uri = TvContract.buildChannelUriForPassthroughInput(inputId);
                Intent i = new Intent(Intent.ACTION_VIEW, uri);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                act.startActivity(i);
                return "ok";
            } catch (Exception ex) {
                return "The TV could not switch to this input.";
            }
        }

        @JavascriptInterface
        public String installed(String pkg) {
            try { act.getPackageManager().getPackageInfo(pkg, 0); return "1"; }
            catch (Exception ex) { return "0"; }
        }

        /** The TV's full Settings page. */
        @JavascriptInterface
        public String openSettings() {
            try {
                Intent i = new Intent(android.provider.Settings.ACTION_SETTINGS);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                act.startActivity(i);
                return "ok";
            } catch (Exception ex) {
                return launch("com.android.tv.settings");
            }
        }

        /**
         * The Home button helper: "on" (Accessibility helper), "watch" (Usage access watch, Android 9),
         * "paused" (watch paused for the Android home screen), "off-usage" (Android 9: switch on Usage
         * access) or "off" (switch on the Accessibility helper).
         */
        @JavascriptInterface
        public String homeHelper() {
            if (HomeWatch.guardOn(act)) return "on";
            if (!HomeWatch.supported()) return "off";
            if (!HomeWatch.granted(act)) return "off-usage";
            return System.currentTimeMillis() < HomeWatch.pausedUntil(act) ? "paused" : "watch";
        }

        /** "1" if Round2 can switch its Home button helper on and off itself (granted by the PC toolkit). */
        @JavascriptInterface
        public String homeGuardSwitchable() { return UpdateReceiver.canSwitchHomeGuard(act) ? "1" : "0"; }

        @JavascriptInterface
        public String setHomeGuard(boolean on) { return UpdateReceiver.setHomeGuard(act, on) ? "ok" : "The TV did not allow this."; }

        /** Staff: the TV's own home screen for 5 minutes. */
        @JavascriptInterface
        public String androidHome() {
            return HomeWatch.openStockHome(act) ? "ok" : "The TV's own home screen could not be opened.";
        }

        /**
         * Opens the screen where staff switch "Round2 Home button" on. Some TVs (Prism+) hide
         * Accessibility from their Settings menu although the page itself is still there, so this
         * tries the standard way first, then the TV settings pages by name.
         */
        @JavascriptInterface
        public String openHomeHelper() {
            // Every page that can switch the Home button over, best first. Some TVs (Prism+) hide
            // these from their Settings menu, or keep a phone-style Settings app underneath that still
            // lists every helper. Each OK press opens the next one that exists, so staff can look in each.
            String[][] cands = {
                {"acc",   null, "com.android.settings", "com.android.settings.Settings$AccessibilitySettingsActivity"},
                {"home",  android.provider.Settings.ACTION_HOME_SETTINGS, null, null},
                {"home",  "android.settings.MANAGE_DEFAULT_APPS_SETTINGS", null, null},
                {"usage", null, "com.android.settings", "com.android.settings.Settings$UsageAccessSettingsActivity"},
                {"usage", android.provider.Settings.ACTION_USAGE_ACCESS_SETTINGS, null, null},
                {"acc",   android.provider.Settings.ACTION_ACCESSIBILITY_SETTINGS, null, null},
                {"acc",   null, "com.android.tv.settings", "com.android.tv.settings.accessibility.AccessibilityActivity"},
                {"acc",   null, "com.android.tv.settings", "com.android.tv.settings.system.AccessibilityActivity"},
            };
            List<Intent> found = new ArrayList<>();
            List<String> kinds = new ArrayList<>();
            Set<String> seen = new HashSet<>();
            PackageManager pm = act.getPackageManager();
            for (String[] c : cands) {
                if (c[0].equals("usage") && (!HomeWatch.supported() || HomeWatch.guardOn(act))) continue;
                Intent i = c[1] != null ? new Intent(c[1]) : new Intent().setClassName(c[2], c[3]);
                try {
                    android.content.ComponentName cn = i.resolveActivity(pm);
                    if (cn == null || !seen.add(cn.flattenToShortString())) continue;
                    i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    found.add(i); kinds.add(c[0]);
                } catch (Exception ignored) { }
            }
            if (found.isEmpty()) return "This TV has no page for the Home button.";
            int n = found.size();
            for (int k = 0; k < n; k++) {
                int idx = (homeTry++) % n;
                try {
                    act.startActivity(found.get(idx));
                    return kinds.get(idx) + "|" + (idx + 1) + "|" + n;
                } catch (Exception ignored) { }
            }
            return "The TV would not open its settings pages.";
        }
        private int homeTry = 0;

        /** Food alerts over other apps (Netflix, HDMI): "on", "off" (needs switching on) or "na" (build without alerts). */
        @JavascriptInterface
        public String foodOverlay() {
            if (!FoodAlerts.enabled()) return "na";
            if (android.os.Build.VERSION.SDK_INT < 23) return "on";
            return android.provider.Settings.canDrawOverlays(act) ? "on" : "off";
        }

        /**
         * Opens a page where staff allow Round2 Home to "display over other apps", best first. Some TVs
         * (Prism+) hide it, so each OK press opens the next page that exists: "overlay|2|4", or a message.
         */
        @JavascriptInterface
        public String openFoodOverlay() {
            android.net.Uri pkg = android.net.Uri.parse("package:" + act.getPackageName());
            Intent[] cands = {
                new Intent(android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION, pkg),
                new Intent(android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION),
                new Intent().setClassName("com.android.tv.settings", "com.android.tv.settings.device.apps.specialaccess.SpecialAppAccessActivity"),
                new Intent().setClassName("com.android.tv.settings", "com.android.tv.settings.device.apps.specialaccess.SystemAlertWindowActivity"),
                new Intent().setClassName("com.android.settings", "com.android.settings.Settings$OverlaySettingsActivity"),
                new Intent().setClassName("com.android.settings", "com.android.settings.Settings$AppDrawOverlaySettingsActivity"),
                new Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, pkg),
            };
            List<Intent> found = new ArrayList<>();
            Set<String> seen = new HashSet<>();
            PackageManager pm = act.getPackageManager();
            for (Intent i : cands) {
                try {
                    android.content.ComponentName cn = i.resolveActivity(pm);
                    if (cn == null || !seen.add(cn.flattenToShortString() + i.getAction() + i.getData())) continue;
                    i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    found.add(i);
                } catch (Exception ignored) { }
            }
            if (found.isEmpty()) return "This TV has no page for it. On the PC: round2tv.ps1 alerts";
            int n = found.size();
            for (int k = 0; k < n; k++) {
                int idx = (overlayTry++) % n;
                try {
                    act.startActivity(found.get(idx));
                    return (found.get(idx).getAction() != null && found.get(idx).getAction().equals(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS) ? "info" : "overlay") + "|" + (idx + 1) + "|" + n;
                } catch (Exception ignored) { }
            }
            return "The TV would not open its settings pages. On the PC: round2tv.ps1 alerts";
        }
        private int overlayTry = 0;

        /** Launchable apps for the "More apps" grid, as [{name, android}]. */
        @JavascriptInterface
        public String apps() {
            JSONArray out = new JSONArray();
            try {
                PackageManager pm = act.getPackageManager();
                Set<String> seen = new HashSet<>();
                Set<String> seenNames = new HashSet<>();
                seen.add(act.getPackageName());
                List<ResolveInfo> found = new ArrayList<>();
                Intent tv = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LEANBACK_LAUNCHER);
                found.addAll(pm.queryIntentActivities(tv, 0));
                List<JSONObject> rows = new ArrayList<>();
                for (ResolveInfo r : found) {
                    String pkg = r.activityInfo.packageName;
                    if (seen.contains(pkg)) continue;
                    ApplicationInfo ai = r.activityInfo.applicationInfo;
                    if (ai != null && !ai.enabled) continue;
                    String name = String.valueOf(r.loadLabel(pm)).trim();
                    // one tile per app name: TCL lists several channel shortcuts under the same name
                    if (name.isEmpty() || seenNames.contains(name.toLowerCase())) continue;
                    seen.add(pkg);
                    seenNames.add(name.toLowerCase());
                    JSONObject o = new JSONObject();
                    o.put("name", name);
                    o.put("android", pkg);
                    o.put("art", "https://r2.icon/" + pkg);
                    o.put("banner", act.bannerOf(pkg) != null);
                    rows.add(o);
                }
                Collections.sort(rows, new Comparator<JSONObject>() {
                    @Override public int compare(JSONObject a, JSONObject b) {
                        return a.optString("name").compareToIgnoreCase(b.optString("name"));
                    }
                });
                for (JSONObject o : rows) out.put(o);
            } catch (Exception ignored) { }
            return out.toString();
        }
    }
}
