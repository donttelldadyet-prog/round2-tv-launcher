package sg.round2.tvhome;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.provider.Settings;
import android.widget.Toast;

/**
 * Two jobs after "Update now":
 *  - status from Android's installer: show its "Install?" screen, or say why it failed;
 *  - MY_PACKAGE_REPLACED (the new version is in): switch the Home-button helper off and on again,
 *    because some TVs (TCL) stop running it after an app update, then reopen the menu.
 */
public class UpdateReceiver extends BroadcastReceiver {

    private static final String GUARD = "sg.round2.tvhome/sg.round2.tvhome.HomeGuard";

    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction())) {
            rearmHomeGuard(ctx);
            HomeWatch.start(ctx);
            try {
                Intent i = new Intent(ctx, MainActivity.class);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ctx.startActivity(i);
            } catch (Exception ignored) { }
            return;
        }
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION && Updater.quiet) {
            Updater.quiet = false;      // started in standby but Android wants a tap: leave it for staff (Update app)
            return;
        }
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) {
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try { ctx.startActivity(confirm); } catch (Exception ignored) { }
            }
        } else if (status != PackageInstaller.STATUS_SUCCESS && !Updater.quiet) {
            String why = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            Toast.makeText(ctx, "Round2 Home update didn't install" + (why != null ? ": " + why : ""), Toast.LENGTH_LONG).show();
        }
    }

    /*
     * The Accessibility setting "Round2 Home button" is the master switch: the app never switches the
     * helper on by itself. (It runs in its own process, so a menu crash can't switch it off.) The only
     * thing done here is restarting it after an app update, and only if it is already on.
     */

    /** Needs WRITE_SECURE_SETTINGS, which the PC toolkit grants with 'homeguard' (and undo removes). */
    static void rearmHomeGuard(Context ctx) {
        if (ctx.checkCallingOrSelfPermission("android.permission.WRITE_SECURE_SETTINGS") != PackageManager.PERMISSION_GRANTED) { FoodAlerts.mlog(ctx, "helper: app updated – no permission to restart the Home-button helper (PC: homeguard)"); return; }
        try {
            String cur = Settings.Secure.getString(ctx.getContentResolver(), "enabled_accessibility_services");
            if (cur == null || !cur.contains(GUARD)) { FoodAlerts.mlog(ctx, "helper: app updated – helper is off, left off"); return; }          // staff never turned it on: leave it
            FoodAlerts.mlog(ctx, "helper: app updated – restarting the Home-button helper (off/on)");
            StringBuilder others = new StringBuilder();
            for (String s : cur.split(":")) if (!s.isEmpty() && !s.equals(GUARD)) others.append(others.length() > 0 ? ":" : "").append(s);
            Settings.Secure.putString(ctx.getContentResolver(), "enabled_accessibility_services", others.toString());
            Thread.sleep(1500);   /* 500 ms wasn't always enough on TCL (Kai, 20 Nov) */
            Settings.Secure.putString(ctx.getContentResolver(), "enabled_accessibility_services", cur);
            Settings.Secure.putString(ctx.getContentResolver(), "accessibility_enabled", "1");
        } catch (Exception ignored) { }
    }

    /* ---- self-repair (owner, 20 Nov: "updating kai fucked up the remote again … ok build for me") ----
       TCL (Kai): the app update stopped the helper's process (the TV's own record: "installPackageLI" for ":guard") and it didn't
       come back healthy; a helper that is ON but not running leaves the remote slow / doubled until someone switches it off and
       on in Accessibility. Now the app checks: switched ON in Accessibility (the master switch – never switched on by the app)
       but its process ":guard" isn't running → off, 1.5 s, on (Android starts it afresh), at most once per 10 minutes, and it
       writes what it did to the TV's log (msgLog, read by the engine's msglog action). Checked 20 s and 60 s after the app
       starts (so right after an update too) and with every 5-minute check-in. TCL only. */
    static final String GUARD_PROC = "sg.round2.tvhome:guard";
    static boolean guardListed(Context c) {
        try { String cur = Settings.Secure.getString(c.getContentResolver(), "enabled_accessibility_services"); return cur != null && cur.contains(GUARD); } catch (Exception e) { return false; }
    }
    /* running = Android has the helper bound (its list of running accessibility services) OR its ":guard" process is alive –
       only when BOTH say no is it treated as stopped, so a healthy helper is never switched off and on by mistake */
    static boolean guardRunning(Context c) {
        try {
            android.view.accessibility.AccessibilityManager acc = (android.view.accessibility.AccessibilityManager) c.getSystemService(Context.ACCESSIBILITY_SERVICE);
            for (android.accessibilityservice.AccessibilityServiceInfo i : acc.getEnabledAccessibilityServiceList(android.accessibilityservice.AccessibilityServiceInfo.FEEDBACK_ALL_MASK))
                if (i.getResolveInfo() != null && i.getResolveInfo().serviceInfo != null && "sg.round2.tvhome.HomeGuard".equals(i.getResolveInfo().serviceInfo.name)) return true;
        } catch (Exception e) { return true; }   /* can't tell: leave it alone */
        try {
            android.app.ActivityManager am = (android.app.ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
            java.util.List<android.app.ActivityManager.RunningAppProcessInfo> L = am.getRunningAppProcesses();
            if (L != null) for (android.app.ActivityManager.RunningAppProcessInfo p : L) if (GUARD_PROC.equals(p.processName)) return true;
        } catch (Exception e) { return true; }
        return false;
    }
    /** for the check-in: "on" (running), "on-stopped", "off"; "+np" = no permission to restart it */
    static String guardState(Context c) {
        boolean on = guardListed(c);
        return (on ? (guardRunning(c) ? "on" : "on-stopped") : "off") + (canSwitchHomeGuard(c) ? "" : "+np");
    }
    private static long lastRepair = 0, lastNoPerm = 0;
    static void guardCheck(final Context c, final String why) {
        if (!HomeGuard.TCL || !guardListed(c) || guardRunning(c)) return;
        final long now = System.currentTimeMillis();
        if (!canSwitchHomeGuard(c)) { if (now - lastNoPerm > 3600_000L) { lastNoPerm = now; FoodAlerts.mlog(c, "helper: ON but not running (" + why + ") – can't restart it: no permission (PC: homeguard)"); } return; }
        if (now - lastRepair < 10 * 60_000L) return;
        lastRepair = now;
        FoodAlerts.mlog(c, "helper: ON but not running (" + why + ") – restarting it (off, 1.5 s, on)");
        final android.content.ContentResolver cr = c.getContentResolver();
        try {
            final String cur = Settings.Secure.getString(cr, "enabled_accessibility_services");
            StringBuilder others = new StringBuilder();
            for (String x : cur.split(":")) if (!x.isEmpty() && !x.equals(GUARD)) others.append(others.length() > 0 ? ":" : "").append(x);
            Settings.Secure.putString(cr, "enabled_accessibility_services", others.toString());
            final android.os.Handler h = new android.os.Handler(android.os.Looper.getMainLooper());
            h.postDelayed(new Runnable() { @Override public void run() {
                try { Settings.Secure.putString(cr, "enabled_accessibility_services", cur); Settings.Secure.putString(cr, "accessibility_enabled", "1"); } catch (Exception ignored) { }
                h.postDelayed(new Runnable() { @Override public void run() {
                    FoodAlerts.mlog(c, "helper: " + (guardRunning(c) ? "running again" : "STILL not running – switch Round2 Home button off and on in Accessibility"));
                } }, 15_000);
            } }, 1500);
        } catch (Exception e) { FoodAlerts.mlog(c, "helper: restart failed – " + e.getClass().getSimpleName()); }
    }

    /** Staff tools › Home button on TVs whose menus hide the helper (Prism+). Needs the same permission. */
    static boolean canSwitchHomeGuard(Context ctx) {
        return ctx.checkCallingOrSelfPermission("android.permission.WRITE_SECURE_SETTINGS") == PackageManager.PERMISSION_GRANTED;
    }

    static boolean setHomeGuard(Context ctx, boolean on) {
        if (!canSwitchHomeGuard(ctx)) return false;
        try {
            String cur = Settings.Secure.getString(ctx.getContentResolver(), "enabled_accessibility_services");
            StringBuilder others = new StringBuilder();
            if (cur != null) for (String s : cur.split(":")) if (!s.isEmpty() && !s.equals(GUARD)) others.append(others.length() > 0 ? ":" : "").append(s);
            if (on) {
                /* (9 Nov, Kai) listed as on but Android had stopped running it (after TCL's Safety Guard closed it): writing the
                   same list again changes nothing, so take it off first and put it back a moment later – Android starts it afresh */
                final String with = others.length() > 0 ? others + ":" + GUARD : GUARD;
                final android.content.ContentResolver cr = ctx.getContentResolver();
                if (cur != null && cur.contains(GUARD)) {
                    Settings.Secure.putString(cr, "enabled_accessibility_services", others.toString());
                    new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() { @Override public void run() {
                        try { Settings.Secure.putString(cr, "enabled_accessibility_services", with); Settings.Secure.putString(cr, "accessibility_enabled", "1"); } catch (Exception ignored) { }
                    } }, 700);
                } else {
                    Settings.Secure.putString(cr, "enabled_accessibility_services", with);
                    Settings.Secure.putString(cr, "accessibility_enabled", "1");
                }
            } else {
                Settings.Secure.putString(ctx.getContentResolver(), "enabled_accessibility_services", others.toString());
            }
            return true;
        } catch (Exception ex) { return false; }
    }
}
