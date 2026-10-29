package sg.round2.tvhome;

import android.app.AppOpsManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.app.usage.UsageEvents;
import android.app.usage.UsageStatsManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ResolveInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Process;
import android.provider.Settings;
import android.widget.Toast;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Home button for TVs that hide the Accessibility helper and have network debugging locked
 * (Prism+, Android 9): no laptop needed, only "Usage access" switched on with the remote.
 *
 * It watches which app is in front. When the TV's own home screen comes up (the Home button,
 * an app closing, start-up) it brings Round2 Home back in front, so the stock home only shows
 * for a moment. Android 10 and newer block this, so it only runs on Android 9 and older, and
 * never while the Accessibility helper (HomeGuard, which is quicker) is on.
 *
 * Ways out for staff: Staff tools › Android home screen (5 minutes), switching Usage access off
 * for Round2 Home (the master switch), and it steps aside by itself if Round2 Home keeps closing.
 * It reads only app names from Android's usage list, keeps nothing and sends nothing.
 */
public class HomeWatch extends Service {

    private static final String PREFS = "homewatch";
    private static final long POLL_MS = 300;
    private static final long CRASH_PAUSE_MS = 10 * 60_000;

    private final Handler main = new Handler(Looper.getMainLooper());
    private UsageStatsManager usm;
    private Set<String> homes;
    private String front = "";
    private long lastQuery, lastRedirect;
    private final long[] recent = new long[6];
    private int recentAt;

    /* ---------- for the menu and the receivers ---------- */

    public static boolean supported() { return Build.VERSION.SDK_INT <= 28; }

    public static boolean granted(Context c) {
        try {
            AppOpsManager ops = (AppOpsManager) c.getSystemService(Context.APP_OPS_SERVICE);
            return ops != null && ops.checkOpNoThrow(AppOpsManager.OPSTR_GET_USAGE_STATS,
                    Process.myUid(), c.getPackageName()) == AppOpsManager.MODE_ALLOWED;
        } catch (Exception ex) { return false; }
    }

    /** The Accessibility helper is really running (not just switched on in the settings list: on Atlas, the
     *  Prism+, it can be listed there while Android never starts it, and then this watch must keep going). */
    public static boolean guardOn(Context c) {
        try {
            String s = Settings.Secure.getString(c.getContentResolver(), Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES);
            if (s == null || !s.contains(c.getPackageName() + "/") || !s.contains("HomeGuard")) return false;
            android.view.accessibility.AccessibilityManager am =
                    (android.view.accessibility.AccessibilityManager) c.getSystemService(Context.ACCESSIBILITY_SERVICE);
            if (am == null) return true;
            for (android.accessibilityservice.AccessibilityServiceInfo i
                    : am.getEnabledAccessibilityServiceList(android.accessibilityservice.AccessibilityServiceInfo.FEEDBACK_ALL_MASK)) {
                if (i.getId() != null && i.getId().startsWith(c.getPackageName() + "/") && i.getId().contains("HomeGuard")) return true;
            }
            return false;
        } catch (Exception ex) { return false; }
    }

    public static void start(Context c) {
        if (!supported() || !granted(c) || guardOn(c)) return;
        try {
            Intent i = new Intent(c, HomeWatch.class);
            if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i);
        } catch (Exception ignored) { }
    }

    public static long pausedUntil(Context c) {
        return c.getSharedPreferences(PREFS, MODE_PRIVATE).getLong("pausedUntil", 0);
    }

    public static void pause(Context c, long ms) {
        c.getSharedPreferences(PREFS, MODE_PRIVATE).edit().putLong("pausedUntil", System.currentTimeMillis() + ms).apply();
    }

    /** The TV's own home screen apps (everything that answers the Home button, except Round2 Home). */
    public static Set<String> homes(Context c) {
        Set<String> out = new HashSet<>();
        try {
            Intent h = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME);
            List<ResolveInfo> list = c.getPackageManager().queryIntentActivities(h, 0);
            for (ResolveInfo r : list) {
                String p = r.activityInfo.packageName;
                if (p.equals(c.getPackageName()) || p.equals("android") || p.equals("com.android.settings")
                        || p.equals("com.android.tv.settings")) continue;   // the "starting up" placeholder lives in Settings
                out.add(p);
            }
        } catch (Exception ignored) { }
        out.add("com.google.android.tvlauncher");
        out.add("com.google.android.leanbacklauncher");
        out.add("com.google.android.apps.tv.launcherx");
        return out;
    }

    /** Opens the TV's own home screen for staff, with the watch paused for 5 minutes. */
    public static boolean openStockHome(Context c) {
        pause(c, 5 * 60_000);
        for (String p : homes(c)) {
            try {
                Intent i = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).setPackage(p);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                if (i.resolveActivity(c.getPackageManager()) == null) continue;
                c.startActivity(i);
                return true;
            } catch (Exception ignored) { }
        }
        return false;
    }

    /* ---------- the service ---------- */

    @Override public IBinder onBind(Intent i) { return null; }

    @Override
    public void onCreate() {
        super.onCreate();
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            NotificationChannel ch = new NotificationChannel("homewatch", "Round2 Home button", NotificationManager.IMPORTANCE_MIN);
            ch.setShowBadge(false);
            if (nm != null) nm.createNotificationChannel(ch);
            b = new Notification.Builder(this, "homewatch");
        } else {
            b = new Notification.Builder(this).setPriority(Notification.PRIORITY_MIN);
        }
        b.setSmallIcon(R.drawable.icon).setContentTitle("Round2 Home button").setContentText("Home opens the Round2 menu").setOngoing(true);
        try { startForeground(4402, b.build()); } catch (Exception ignored) { }
        usm = (UsageStatsManager) getSystemService(Context.USAGE_STATS_SERVICE);
        homes = homes(this);
        lastQuery = System.currentTimeMillis() - 15_000;   // start-up: catch the home screen that is already showing
        main.post(tick);
    }

    @Override
    public int onStartCommand(Intent i, int flags, int id) { return START_STICKY; }

    @Override
    public void onDestroy() { main.removeCallbacks(tick); super.onDestroy(); }

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!granted(HomeWatch.this) || guardOn(HomeWatch.this)) { stopForeground(true); stopSelf(); return; }
            try { check(); } catch (Exception ignored) { }
            main.postDelayed(this, POLL_MS);
        }
    };

    private void check() {
        if (usm == null) return;
        long now = System.currentTimeMillis();
        UsageEvents ev = usm.queryEvents(lastQuery, now + 1000);
        lastQuery = now - 1000;   // a little overlap: events can be written late
        UsageEvents.Event e = new UsageEvents.Event();
        String newest = null; long newestAt = 0;
        while (ev.hasNextEvent()) {
            ev.getNextEvent(e);
            if (e.getEventType() == UsageEvents.Event.MOVE_TO_FOREGROUND && e.getTimeStamp() >= newestAt) {
                newest = e.getPackageName(); newestAt = e.getTimeStamp();
            }
        }
        if (newest == null) return;
        boolean wasHome = homes.contains(front);
        front = newest;
        if (!homes.contains(newest) || newestAt <= lastRedirect) return;
        if (wasHome && now - newestAt > 3000) return;            // old news from the overlap
        if (now < pausedUntil(this)) return;                     // staff opened the Android home screen
        if (now - lastRedirect < 800) return;
        long oldest = recent[recentAt];
        recent[recentAt] = now; recentAt = (recentAt + 1) % recent.length;
        if (oldest > 0 && now - oldest < 30_000) {               // Round2 Home keeps closing: step aside
            pause(this, CRASH_PAUSE_MS);
            try { Toast.makeText(this, "Round2 menu keeps closing · Android home for 10 minutes", Toast.LENGTH_LONG).show(); } catch (Exception ignored) { }
            return;
        }
        lastRedirect = now;
        Intent i = new Intent(this, MainActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        i.putExtra("home", true);
        try { startActivity(i); } catch (Exception ignored) { }
    }
}
