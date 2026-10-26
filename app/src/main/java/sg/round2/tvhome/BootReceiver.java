package sg.round2.tvhome;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Build;

/**
 * TV switched on. On TVs where Round2 Home is not the home app and no Home-button helper could be
 * switched on (Prism+), open the menu ourselves so guests still start in Round2. Android 9 and older
 * only: newer Android blocks this, and those TVs are set up with the helper or as the home app.
 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context ctx, Intent intent) {
        HomeWatch.start(ctx);
        if (Build.VERSION.SDK_INT > 28 || HomeWatch.guardOn(ctx)) return;
        try {
            Intent home = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME);
            ResolveInfo r = ctx.getPackageManager().resolveActivity(home, PackageManager.MATCH_DEFAULT_ONLY);
            if (r != null && r.activityInfo != null && ctx.getPackageName().equals(r.activityInfo.packageName)) return;
            Intent i = new Intent(ctx, MainActivity.class);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(i);
        } catch (Exception ignored) { }
    }
}
