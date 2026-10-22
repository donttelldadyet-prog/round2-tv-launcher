package sg.round2.tvhome;

import android.accessibilityservice.AccessibilityService;
import android.content.Intent;
import android.view.accessibility.AccessibilityEvent;

/**
 * Google TV won't let another app replace its home screen, and disabling it
 * triggers a recovery loop. Instead, this service watches for the stock home
 * screen opening (the remote's Home button) and brings Round2 Home to the front.
 * It only receives window-change events from the home-screen packages listed in
 * res/xml/home_guard.xml and reads no screen content.
 * Turn it on or off with round2tv.ps1 homeguard / undo.
 */
public class HomeGuard extends AccessibilityService {

    private long lastRedirect;

    @Override
    public void onAccessibilityEvent(AccessibilityEvent e) {
        if (e == null || e.getEventType() != AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) return;
        CharSequence pkg = e.getPackageName();
        if (pkg == null) return;
        String p = pkg.toString();
        if (!p.equals("com.google.android.apps.tv.launcherx") && !p.equals("com.google.android.tvlauncher")) return;
        long now = System.currentTimeMillis();
        if (now - lastRedirect < 800) return;   // avoid bouncing
        lastRedirect = now;
        Intent i = new Intent(this, MainActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivity(i);
    }

    @Override
    public void onInterrupt() { }
}
