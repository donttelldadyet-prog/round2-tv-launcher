package sg.round2.tvhome;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * Event backgrounds (Halloween, CNY, ...) pushed from GitHub without reinstalling the app.
 *
 * The "Publish themes" GitHub action encodes each theme video for TVs and puts it, with
 * themes.json, on the repo's "themes" release. This class downloads that list, keeps the
 * videos this room needs (live now or starting within two weeks) in the app's own storage,
 * checks each file's SHA-256, and deletes ones that have ended. The menu shows them under
 * Options > Background and switches to a "takeover" theme while its dates are live.
 */
final class ThemeSync {

    static final String MANIFEST_URL =
            "https://github.com/donttelldadyet-prog/round2-tv-launcher/releases/download/themes/themes.json";
    private static final int EARLY_DAYS = 14;   // download this many days before a theme starts

    final File dir;
    private volatile boolean running;

    interface Done { void done(String json); }

    ThemeSync(Context ctx) {
        dir = new File(ctx.getFilesDir(), "themes");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
    }

    /** The last downloaded list, each theme marked ready:true when its video is on the TV. */
    String cached() {
        try {
            File m = new File(dir, "themes.json");
            if (!m.exists()) return "{\"themes\":[]}";
            JSONObject root = new JSONObject(readAll(new FileInputStream(m)));
            JSONArray ts = root.optJSONArray("themes");
            if (ts == null) ts = new JSONArray();
            for (int i = 0; i < ts.length(); i++) {
                JSONObject t = ts.getJSONObject(i);
                String id = safeId(t.optString("id"));
                t.put("ready", id != null && new File(dir, id + ".mp4").exists());
            }
            root.put("themes", ts);
            return root.toString();
        } catch (Exception e) {
            return "{\"themes\":[]}";
        }
    }

    /** Fetch the list and the videos this room needs, on a background thread. */
    void sync(final String room, final Done cb) {
        if (running) return;
        running = true;
        new Thread(new Runnable() {
            @Override public void run() {
                try { doSync(room == null ? "" : room); } catch (Exception ignored) { }
                running = false;
                if (cb != null) cb.done(cached());
            }
        }, "r2-theme-sync").start();
    }

    private void doSync(String room) throws Exception {
        byte[] body = get(MANIFEST_URL);
        if (body == null) return;                      // offline or not published yet: keep what we have
        JSONObject root = new JSONObject(new String(body, "UTF-8"));
        JSONArray ts = root.optJSONArray("themes");
        if (ts == null) return;
        write(new File(dir, "themes.json"), body);

        SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd", Locale.US);
        String today = f.format(Calendar.getInstance().getTime());
        Calendar soon = Calendar.getInstance(); soon.add(Calendar.DATE, EARLY_DAYS);
        String horizon = f.format(soon.getTime());

        Set<String> keep = new HashSet<>();
        keep.add("themes.json");
        for (int i = 0; i < ts.length(); i++) {
            JSONObject t = ts.getJSONObject(i);
            String id = safeId(t.optString("id"));
            if (id == null || !forRoom(t, room)) continue;
            String from = t.optString("from", ""), until = t.optString("until", "");
            if (!until.isEmpty() && until.compareTo(today) < 0) continue;     // ended
            if (!from.isEmpty() && from.compareTo(horizon) > 0) continue;    // too far ahead
            keep.add(id + ".mp4"); keep.add(id + ".jpg");
            fetchFile(t.optString("thumb"), new File(dir, id + ".jpg"), null);
            fetchFile(t.optString("video"), new File(dir, id + ".mp4"), t.optString("sha256", null));
        }
        File[] all = dir.listFiles();
        if (all != null) for (File x : all) if (!keep.contains(x.getName())) //noinspection ResultOfMethodCallIgnored
            x.delete();
    }

    private static boolean forRoom(JSONObject t, String room) {
        JSONArray rooms = t.optJSONArray("rooms");
        if (rooms == null || rooms.length() == 0) return true;
        for (int i = 0; i < rooms.length(); i++) {
            String r = rooms.optString(i);
            if ("all".equalsIgnoreCase(r) || r.equalsIgnoreCase(room)) return true;
        }
        return false;
    }

    private void fetchFile(String url, File out, String sha) throws Exception {
        if (url == null || url.isEmpty() || !url.startsWith("https://")) return;
        if (out.exists() && (sha == null || sha.equalsIgnoreCase(sha256(out)))) return;
        File part = new File(out.getPath() + ".part");
        HttpURLConnection c = open(url);
        if (c == null) return;
        try (InputStream in = c.getInputStream(); OutputStream o = new FileOutputStream(part)) {   // streamed: no big copy in memory
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
        } finally { c.disconnect(); }
        if (sha != null && !sha.isEmpty() && !sha.equalsIgnoreCase(sha256(part))) {   // damaged download
            //noinspection ResultOfMethodCallIgnored
            part.delete();
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        out.delete();
        //noinspection ResultOfMethodCallIgnored
        part.renameTo(out);
    }

    /** HTTPS GET, following GitHub's redirects to its download servers. */
    private static byte[] get(String url) {
        HttpURLConnection c = open(url);
        if (c == null) return null;
        try { return readBytes(c.getInputStream()); } catch (Exception e) { return null; } finally { c.disconnect(); }
    }

    static HttpURLConnection open(String url) {
        try {
            for (int hop = 0; hop < 6; hop++) {
                HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
                c.setInstanceFollowRedirects(false);
                c.setConnectTimeout(15000);
                c.setReadTimeout(60000);
                c.setRequestProperty("User-Agent", "Round2Home");
                int code = c.getResponseCode();
                if (code >= 300 && code < 400) {
                    String loc = c.getHeaderField("Location");
                    c.disconnect();
                    if (loc == null || !loc.startsWith("https://")) return null;
                    url = loc;
                    continue;
                }
                if (code != 200) { c.disconnect(); return null; }
                return c;
            }
        } catch (Exception ignored) { }
        return null;
    }

    static String safeId(String id) {
        return id != null && id.matches("[a-z0-9][a-z0-9-]{0,40}") ? id : null;
    }

    private static String sha256(File f) {
        try (InputStream in = new FileInputStream(f)) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte b : md.digest()) sb.append(String.format(Locale.US, "%02x", b));
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }

    private static void write(File f, byte[] data) throws Exception {
        try (OutputStream o = new FileOutputStream(f)) { o.write(data); }
    }

    private static byte[] readBytes(InputStream in) throws Exception {
        try (InputStream s = in) {
            java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            int n;
            while ((n = s.read(buf)) > 0) bo.write(buf, 0, n);
            return bo.toByteArray();
        }
    }

    private static String readAll(InputStream in) throws Exception {
        return new String(readBytes(in), "UTF-8");
    }
}
