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
        if (ctx.checkCallingOrSelfPermission("android.permission.WRITE_SECURE_SETTINGS") != PackageManager.PERMISSION_GRANTED) return;
        try {
            String cur = Settings.Secure.getString(ctx.getContentResolver(), "enabled_accessibility_services");
            if (cur == null || !cur.contains(GUARD)) return;          // staff never turned it on: leave it
            StringBuilder others = new StringBuilder();
            for (String s : cur.split(":")) if (!s.isEmpty() && !s.equals(GUARD)) others.append(others.length() > 0 ? ":" : "").append(s);
            Settings.Secure.putString(ctx.getContentResolver(), "enabled_accessibility_services", others.toString());
            Thread.sleep(500);
            Settings.Secure.putString(ctx.getContentResolver(), "enabled_accessibility_services", cur);
            Settings.Secure.putString(ctx.getContentResolver(), "accessibility_enabled", "1");
        } catch (Exception ignored) { }
    }
}
