package sg.round2.tvhome;

import android.content.Context;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

/**
 * ONE CLOCK (owner, 2 Nov 2026): message and food-alert times and theme dates run on the booking engine's clock
 * (Cloudflare /time), not the TV's own – a TV whose clock drifted or sits in the wrong zone still shows "That's a wrap!"
 * on time. sync() measures the difference (every 30 min from FoodAlerts); now() = the TV's time + that difference.
 */
final class Clock {
    private static final String URL_TIME = "https://round2-book.donttelldadyet.workers.dev/time";
    private static volatile long off = Long.MIN_VALUE;

    private Clock() {}

    static long now() { return System.currentTimeMillis() + (off == Long.MIN_VALUE ? 0 : off); }

    static void load(Context c) {
        if (off == Long.MIN_VALUE) off = c.getSharedPreferences("r2clock", Context.MODE_PRIVATE).getLong("off", 0);
    }

    /** Singapore date (yyyy-MM-dd) of the shop night, by the engine's clock: before 7am it's still last night (owner, 2 Nov). */
    static String sgDate(int days) {
        SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd", Locale.US);
        f.setTimeZone(TimeZone.getTimeZone("Asia/Singapore"));
        return f.format(new Date(now() - 7 * 3_600_000L + days * 86_400_000L));
    }

    /** Ask the engine for its time (call off the main thread). */
    static void sync(Context c) {
        HttpURLConnection h = null;
        try {
            long a = System.currentTimeMillis();
            h = (HttpURLConnection) new URL(URL_TIME).openConnection();
            h.setConnectTimeout(8000); h.setReadTimeout(8000);
            StringBuilder sb = new StringBuilder();
            try (BufferedReader r = new BufferedReader(new InputStreamReader(h.getInputStream(), "UTF-8"))) { String l; while ((l = r.readLine()) != null) sb.append(l); }
            long b = System.currentTimeMillis();
            long t = new JSONObject(sb.toString()).optLong("t", 0);
            if (t <= 0 || b - a > 5000) return;
            off = t - (a + b) / 2;
            c.getSharedPreferences("r2clock", Context.MODE_PRIVATE).edit().putLong("off", off).apply();
        } catch (Exception ignored) {
        } finally { if (h != null) h.disconnect(); }
    }
}
