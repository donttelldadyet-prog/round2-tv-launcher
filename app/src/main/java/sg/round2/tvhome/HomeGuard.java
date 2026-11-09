package sg.round2.tvhome;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.AccessibilityServiceInfo;
import android.os.Build;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityEvent;
import android.widget.Toast;

/**
 * The remote's Home button opens Round2 Home on TVs that keep their own home screen.
 *
 * 1. It catches the Home key itself, before the TV's home screen opens, and brings Round2 Home
 *    to the front, so the stock home screen never flashes up (TVs that allow this, e.g. Sony).
 * 2. If the stock home screen opens anyway (a TV that doesn't pass the key on, an app that
 *    closes back to it, a crash), it sees that window open and brings Round2 Home back.
 *
 * Ways out for staff, always available:
 *  - Hold Home for 2 seconds, or Options › Staff mode › Android home screen: the Android home screen opens and Round2 Home stays out of the
 *    way for 5 minutes (settings, installing apps, fixing things).
 *  - If Round2 Home keeps closing (4 times in a minute, each time within 2 s of opening), the helper stops for 10 minutes by itself.
 *  - Settings > Accessibility > "Round2 Home button" Off is the master switch.
 *  - From a computer: round2tv.ps1 rescue.
 *
 * It only receives window changes from the home-screen packages in res/xml/home_guard.xml plus
 * key presses, keeps nothing, and reads no screen content.
 */
public class HomeGuard extends AccessibilityService {

    private static final long HOLD_MS = 2000;            // hold Home this long for the Android home screen
    private static final long STAFF_PAUSE_MS = 5 * 60_000;
    private static final long CRASH_PAUSE_MS = 10 * 60_000;

    private final Handler main = new Handler(Looper.getMainLooper());
    private long lastRedirect, pausedUntil, heldPassed;
    private final long[] recent = new long[4];           // times of the last window-change redirects
    private int recentAt;

    private boolean paused() { return System.currentTimeMillis() < pausedUntil; }

    /* Buttons (Kai, 6 Nov): with key filtering on, every button passes through this helper first. When Android slows the
       helper down (during a long Netflix film) a button's release arrives late, the TV treats it as held and repeats it –
       the "multi-press", volume moving by itself (owner's find: buttons were fine exactly while the helper had crashed).
       Not catching keys at all meant a flash of the Google TV home on every Home press, which the owner rejected.
       So: catch keys as before, but time every button; if one reaches us late (the helper is being slowed), let go of the
       buttons at once (Home then works by watching the home screen, 2. below), and take them back as soon as the guest is
       on Round2 Home again. */
    private static final long LAG_MS = 250;
    static final boolean TCL = Build.MANUFACTURER != null && Build.MANUFACTURER.toLowerCase().contains("tcl");
    private volatile boolean keysOn = true;
    private long lastOurs;

    private void setKeys(boolean on) {
        if (keysOn == on) return;
        try {
            AccessibilityServiceInfo i = getServiceInfo();
            if (on) i.flags |= AccessibilityServiceInfo.FLAG_REQUEST_FILTER_KEY_EVENTS; else i.flags &= ~AccessibilityServiceInfo.FLAG_REQUEST_FILTER_KEY_EVENTS;
            setServiceInfo(i);
            keysOn = on;
        } catch (Exception ignored) { }
    }

    /* Options › Staff mode › Android home screen (the menu runs in another process): step aside for 5 minutes */
    private final android.content.BroadcastReceiver pauseRx = new android.content.BroadcastReceiver() {
        @Override public void onReceive(android.content.Context c, Intent i) { pause(STAFF_PAUSE_MS, "Android home screen · Round2 menu paused for 5 minutes"); }
    };

    @Override
    public void onDestroy() { try { unregisterReceiver(pauseRx); } catch (Exception ignored) { } super.onDestroy(); }

    @Override
    protected void onServiceConnected() {
        GuardKeep.start(this);   /* this process at foreground priority (6 Nov) */
        try {
            android.content.IntentFilter ff = new android.content.IntentFilter(HomeWatch.PAUSE);
            if (Build.VERSION.SDK_INT >= 33) registerReceiver(pauseRx, ff, android.content.Context.RECEIVER_NOT_EXPORTED); else registerReceiver(pauseRx, ff);
        } catch (Exception ignored) { }
    }

    private void pause(long ms, String msg) {
        pausedUntil = System.currentTimeMillis() + ms;
        try { Toast.makeText(this, msg, Toast.LENGTH_LONG).show(); } catch (Exception ignored) { }
    }

    private void bringFront() {
        long now = System.currentTimeMillis();
        if (now - lastRedirect < 500) return;            // avoid bouncing
        lastRedirect = now;
        Intent i = new Intent(this, MainActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        i.putExtra("home", true);
        startActivity(i);
    }

    /* 1. the Home key itself */
    @Override
    protected boolean onKeyEvent(KeyEvent k) {
        /* (6 Nov, later) no more letting go of the buttons when one arrives late: on Kai it tripped with the helper healthy,
           so Home showed the Google TV home first and the volume went back to moving twice. The helper's process now runs at
           foreground priority (GuardKeep) instead, which is what kept it from being slowed in the first place. */
        /* TCL (Kai, 6 Nov): a volume press passed back by this helper moves the volume twice (owner: once with the helper off,
           twice with it on, every other button fine). So on TCL the helper takes the volume buttons and changes the volume
           itself – once per press (held = repeats as normal), with the TV's own volume bar. */
        int kc = k.getKeyCode();
        if (TCL && (kc == KeyEvent.KEYCODE_VOLUME_UP || kc == KeyEvent.KEYCODE_VOLUME_DOWN || kc == KeyEvent.KEYCODE_VOLUME_MUTE)) {
            /* (9 Nov, Kai – measured) Android's own volume list showed every press moving the volume from the system itself
               (WindowManager: one call on press, one on release = one step) before this helper sees the button, plus the
               helper's own change = two steps. So on TCL the helper only takes the button (passing it back made it count
               twice too, 6 Nov) and leaves the volume alone. */
            return true;
        }
        if (kc != KeyEvent.KEYCODE_HOME || paused()) return false;
        long held = k.getEventTime() - k.getDownTime();
        if (k.getAction() == KeyEvent.ACTION_DOWN) {
            if (held >= HOLD_MS && heldPassed != k.getDownTime()) openAndroidHome(k.getDownTime());
            return true;                                 // wait for the release to decide
        }
        if (k.getAction() == KeyEvent.ACTION_UP) {
            if (heldPassed == k.getDownTime()) return true;      // already handled while held
            if (held >= HOLD_MS) openAndroidHome(k.getDownTime());
            else bringFront();
            return true;
        }
        return false;
    }

    private void openAndroidHome(long downTime) {
        heldPassed = downTime;
        pause(STAFF_PAUSE_MS, "Android home screen · Round2 menu paused for 5 minutes");
        main.post(new Runnable() { @Override public void run() { performGlobalAction(GLOBAL_ACTION_HOME); } });
    }

    /* 2. the stock home screen opened anyway */
    @Override
    public void onAccessibilityEvent(AccessibilityEvent e) {
        if (e == null || e.getEventType() != AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) return;
        CharSequence pkg = e.getPackageName();
        if (pkg == null) return;
        String p = pkg.toString();
        if (HomeWatch.seen(p)) HomeWatch.tellMain(this, p);   // what's on screen, for the control centre (3 Nov; passed to the main process 5 Nov)
        if (p.equals(getPackageName())) { lastOurs = System.currentTimeMillis(); setKeys(true); }   // (keys stay on; kept in case something switched them off)
        if (paused()) return;
        if (!p.equals("com.google.android.apps.tv.launcherx") && !p.equals("com.google.android.tvlauncher")
                && !p.equals("com.google.android.leanbacklauncher")) return;   // Google TV, Android TV 8-11, older Android TV (e.g. Sony 2015)
        // Only the home screen itself, not other windows such as the Settings panel opened from the remote.
        CharSequence cls = e.getClassName();
        String c = cls == null ? "" : cls.toString();
        if (!c.endsWith(".HomeActivity") && !c.endsWith(".MainActivity")) return;
        long now = System.currentTimeMillis();
        if (now - lastRedirect < 800 && keysOn) return;
        /* If Round2 Home keeps closing (a crash loop), step aside so the TV stays usable. While buttons are let go, Home
           presses also land here: only count it when Round2 Home was up for under 2 s (a crash), not a guest pressing Home
           (owner, 6 Nov: pressing Home a few times paused the menu for 10 minutes). */
        if (!keysOn && lastOurs > 0 && now - lastOurs > 2000) { bringFront(); return; }
        long oldest = recent[recentAt];
        recent[recentAt] = now; recentAt = (recentAt + 1) % recent.length;
        if (oldest > 0 && now - oldest < 60_000) {
            pause(CRASH_PAUSE_MS, "Round2 menu keeps closing · Android home for 10 minutes");
            return;
        }
        bringFront();
    }

    @Override
    public void onInterrupt() { }
}
