package sg.round2.tvhome;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;

/**
 * "Update now" (Options > Staff mode on the TV): checks the latest GitHub release of this app,
 * downloads round2-home.apk and hands it to Android's installer, which asks staff to press Install.
 * Builds are signed with the same key, so the update installs over this version and keeps its settings.
 */
final class Updater {

    static final String LATEST = "https://api.github.com/repos/donttelldadyet-prog/round2-tv-launcher/releases/latest";

    interface Report { void report(JSONObject state); }

    private final Context ctx;
    private volatile boolean busy;
    /** true while an install was started in the background (TV in standby): never show Android's Install screen then */
    static volatile boolean quiet;
    private String apkUrl, latest;

    Updater(Context ctx) { this.ctx = ctx.getApplicationContext(); }

    String current() {
        try { return ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0).versionName; } catch (Exception e) { return "0"; }
    }

    /** a.b.c comparison: is "1.0.38" newer than "1.0.37"? */
    static boolean newer(String a, String b) {
        String[] x = a.split("\\."), y = b.split("\\.");
        for (int i = 0; i < Math.max(x.length, y.length); i++) {
            int p = i < x.length ? num(x[i]) : 0, q = i < y.length ? num(y[i]) : 0;
            if (p != q) return p > q;
        }
        return false;
    }
    private static int num(String s) { try { return Integer.parseInt(s.replaceAll("[^0-9]", "")); } catch (Exception e) { return 0; } }

    void check(final Report r) {
        if (busy) return;
        busy = true;
        new Thread(new Runnable() { @Override public void run() {
            JSONObject o = new JSONObject();
            try {
                o.put("current", current());
                HttpURLConnection c = ThemeSync.open(LATEST);
                if (c == null) { o.put("state", "offline"); }
                else {
                    JSONObject rel = new JSONObject(read(c.getInputStream())); c.disconnect();
                    latest = rel.optString("tag_name", "").replaceFirst("^v", "");
                    apkUrl = null;
                    JSONArray as = rel.optJSONArray("assets");
                    for (int i = 0; as != null && i < as.length(); i++)
                        if ("round2-home.apk".equals(as.getJSONObject(i).optString("name"))) apkUrl = as.getJSONObject(i).optString("browser_download_url");
                    o.put("latest", latest);
                    o.put("state", apkUrl != null && newer(latest, current()) ? "available" : "latest");
                }
            } catch (Exception e) { try { o.put("state", "offline"); } catch (Exception ignored) { } }
            busy = false;
            r.report(o);
        } }, "r2-update-check").start();
    }

    /** A newer version was found by the last check. */
    boolean hasUpdate() { return apkUrl != null && latest != null && newer(latest, current()); }

    /** Android 12+ can update the app with no one pressing Install: done while the TV is in standby. */
    static boolean canInstallQuietly() { return Build.VERSION.SDK_INT >= 31; }

    void install(final Report r) { install(r, false); }

    void install(final Report r, final boolean background) {
        if (busy || apkUrl == null) return;
        if (background && (!canInstallQuietly() || Build.VERSION.SDK_INT < 26 || !ctx.getPackageManager().canRequestPackageInstalls())) return;
        quiet = background;
        // Android 8+: Round2 Home needs "Install unknown apps" once (the PC toolkit allows it; or do it here)
        if (Build.VERSION.SDK_INT >= 26 && !ctx.getPackageManager().canRequestPackageInstalls()) {
            try {
                Intent i = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + ctx.getPackageName()));
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ctx.startActivity(i);
            } catch (Exception ignored) { }
            r.report(state("permission", 0));
            return;
        }
        busy = true;
        new Thread(new Runnable() { @Override public void run() {
            PackageInstaller.Session s = null;
            try {
                HttpURLConnection c = ThemeSync.open(apkUrl);
                if (c == null) throw new Exception("download");
                long total = c.getContentLength();
                PackageInstaller pi = ctx.getPackageManager().getPackageInstaller();
                PackageInstaller.SessionParams p = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
                p.setAppPackageName(ctx.getPackageName());
                if (background) p.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
                int id = pi.createSession(p);
                s = pi.openSession(id);
                try (InputStream in = c.getInputStream(); OutputStream out = s.openWrite("round2-home.apk", 0, total > 0 ? total : -1)) {
                    byte[] buf = new byte[65536];
                    long done = 0; int n, lastPct = -1;
                    while ((n = in.read(buf)) > 0) {
                        out.write(buf, 0, n); done += n;
                        int pct = total > 0 ? (int) (done * 100 / total) : 0;
                        if (pct / 10 != lastPct / 10) { lastPct = pct; r.report(state("downloading", pct)); }
                    }
                    s.fsync(out);
                } finally { c.disconnect(); }
                Intent done = new Intent(ctx, UpdateReceiver.class);
                int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? 0x02000000 /* FLAG_MUTABLE */ : 0);
                PendingIntent pend = PendingIntent.getBroadcast(ctx, id, done, flags);
                r.report(state("installing", 100));
                s.commit(pend.getIntentSender());   // Android shows "Do you want to install an update?": press Install
                s.close();
            } catch (Exception e) {
                if (s != null) try { s.abandon(); } catch (Exception ignored) { }
                r.report(state("failed", 0));
            }
            busy = false;
        } }, "r2-update-install").start();
    }

    private JSONObject state(String st, int pct) {
        JSONObject o = new JSONObject();
        try { o.put("state", st); o.put("pct", pct); o.put("latest", latest); o.put("current", current()); } catch (Exception ignored) { }
        return o;
    }

    private static String read(InputStream in) throws Exception {
        java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[16384]; int n;
        try (InputStream s = in) { while ((n = s.read(buf)) > 0) b.write(buf, 0, n); }
        return b.toString("UTF-8");
    }
}
