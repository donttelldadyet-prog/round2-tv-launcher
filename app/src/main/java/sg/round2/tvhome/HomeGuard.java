package sg.round2.tvhome;

import android.accessibilityservice.AccessibilityService;
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
 *  - Hold Home for 2 seconds: the Android home screen opens and Round2 Home stays out of the
 *    way for 5 minutes (settings, installing apps, fixing things).
 *  - If Round2 Home keeps closing (4 times in a minute), the helper stops for 10 minutes by itself.
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
        if (k.getKeyCode() != KeyEvent.KEYCODE_HOME || paused()) return false;
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
        if (e == null || e.getEventType() != AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED || paused()) return;
        CharSequence pkg = e.getPackageName();
        if (pkg == null) return;
        String p = pkg.toString();
        if (!p.equals("com.google.android.apps.tv.launcherx") && !p.equals("com.google.android.tvlauncher")
                && !p.equals("com.google.android.leanbacklauncher")) return;   // Google TV, Android TV 8-11, older Android TV (e.g. Sony 2015)
        // Only the home screen itself, not other windows such as the Settings panel opened from the remote.
        CharSequence cls = e.getClassName();
        String c = cls == null ? "" : cls.toString();
        if (!c.endsWith(".HomeActivity") && !c.endsWith(".MainActivity")) return;
        long now = System.currentTimeMillis();
        if (now - lastRedirect < 800) return;
        // If Round2 Home keeps closing (a crash loop), step aside so the TV stays usable
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
