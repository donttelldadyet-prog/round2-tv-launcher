package sg.round2.tvhome;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;

/**
 * Keeps the Home-button helper's process (":guard") at foreground priority (owner, 6 Nov: Kai's helper got slowed down and
 * then stopped during a long Netflix film – Android throttles and closes background processes first, a foreground service
 * almost never). Runs in the helper's own process, does nothing else, shows only a silent minimum-priority notification.
 */
public class GuardKeep extends Service {

    /* from the menu (app in front – always allowed) and from the helper itself (best effort) */
    static void start(Context c) {
        try {
            Intent i = new Intent(c, GuardKeep.class);
            if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i);
        } catch (Exception ignored) { }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            NotificationChannel ch = new NotificationChannel("guard", "Round2 Home button", NotificationManager.IMPORTANCE_MIN);
            ch.setShowBadge(false);
            if (nm != null) nm.createNotificationChannel(ch);
            b = new Notification.Builder(this, "guard");
        } else {
            b = new Notification.Builder(this).setPriority(Notification.PRIORITY_MIN);
        }
        b.setSmallIcon(R.drawable.icon).setContentTitle("Round2 Home button").setContentText("Keeps the Home button opening Round2").setOngoing(true);
        try { startForeground(4405, b.build()); } catch (Exception e) { stopSelf(); }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) { return START_STICKY; }

    @Override
    public IBinder onBind(Intent intent) { return null; }
}
