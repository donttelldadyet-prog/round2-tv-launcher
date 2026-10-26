package sg.round2.tvhome;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** TV switched on: start the Home-button watch (Android 9 TVs without the Accessibility helper). */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context ctx, Intent intent) {
        HomeWatch.start(ctx);
    }
}
