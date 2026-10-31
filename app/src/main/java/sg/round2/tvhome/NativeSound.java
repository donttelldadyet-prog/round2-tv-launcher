package sg.round2.tvhome;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.SoundPool;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * Menu sounds and the rain ambience, played by Android itself instead of the web page.
 *
 * The menu still designs the sounds (it renders them once into short WAV clips and a 6 s rain loop);
 * this class only plays them: SoundPool for the clicks and thunder, a looping AudioTrack for the rain.
 * Some TVs (the 2015 Sony) give the web page's own audio no output at all; this path works on any
 * Android TV and costs the menu nothing while it animates.
 */
final class NativeSound {

    private static final String TAG = "R2Home";
    private final Context ctx;
    private final SoundPool pool;
    private final Map<String, Integer> ids = new HashMap<>();
    private final Set<Integer> ready = new HashSet<>();
    private final Handler main = new Handler(Looper.getMainLooper());
    private AudioTrack rain;
    private float rainVol = 0f, rainTarget = 0f, rainStep = 0f;
    private byte[] loopPcm;          // kept so the rain loop can be rebuilt after standby (revive)
    private int loopRate;

    NativeSound(Context ctx) {
        this.ctx = ctx.getApplicationContext();
        AudioAttributes a = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build();
        pool = new SoundPool.Builder().setMaxStreams(6).setAudioAttributes(a).build();
        pool.setOnLoadCompleteListener(new SoundPool.OnLoadCompleteListener() {
            @Override public void onLoadComplete(SoundPool p, int id, int status) {
                if (status == 0) synchronized (ready) { ready.add(id); }
                else Log.w(TAG, "sound load failed " + id + " status " + status);
            }
        });
    }

    /** A short sound clip (WAV bytes) under a name like "neon.move". */
    synchronized void load(String key, byte[] wav) {
        try {
            File f = new File(ctx.getCacheDir(), "sfx-" + key.replaceAll("[^A-Za-z0-9.]", "_") + ".wav");
            try (FileOutputStream o = new FileOutputStream(f)) { o.write(wav); }
            Integer old = ids.remove(key);
            if (old != null) { pool.unload(old); synchronized (ready) { ready.remove(old); } }
            ids.put(key, pool.load(f.getPath(), 1));
        } catch (Exception e) { Log.w(TAG, "sound save failed " + key, e); }
    }

    boolean has(String key) {
        Integer id;
        synchronized (this) { id = ids.get(key); }
        if (id == null) return false;
        synchronized (ready) { return ready.contains(id); }
    }

    void play(String key, float vol) {
        Integer id;
        synchronized (this) { id = ids.get(key); }
        if (id == null) return;
        synchronized (ready) { if (!ready.contains(id)) return; }
        float v = Math.max(0f, Math.min(1f, vol));
        pool.play(id, v, v, 1, 0, 1f);
    }

    /** The rain loop: 16-bit mono PCM, played on repeat. */
    synchronized void loadLoop(byte[] pcm, int sampleRate) {
        stopLoop();
        loopPcm = pcm; loopRate = sampleRate;
        try {
            AudioAttributes a = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build();
            AudioFormat f = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(sampleRate).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build();
            rain = new AudioTrack(a, f, pcm.length, AudioTrack.MODE_STATIC, AudioManager.AUDIO_SESSION_ID_GENERATE);
            rain.write(pcm, 0, pcm.length);
            rain.setLoopPoints(0, pcm.length / 2, -1);
            rain.setVolume(0f);
            rainVol = 0f;
        } catch (Exception e) { Log.w(TAG, "rain loop failed", e); rain = null; }
    }

    boolean hasLoop() { return rain != null; }

    /** Fade the rain to a volume (0 = silent, then paused) over ms. */
    void loopTo(final float vol, final int ms) {
        main.post(new Runnable() { @Override public void run() {
            if (rain == null) return;
            rainTarget = Math.max(0f, Math.min(1f, vol));
            int steps = Math.max(1, ms / 40);
            rainStep = (rainTarget - rainVol) / steps;
            try { if (rainTarget > 0 && rain.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) rain.play(); } catch (Exception ignored) { }
            if (rainTarget > 0) watch();
            main.removeCallbacks(fader);
            main.post(fader);
        } });
    }

    private final Runnable fader = new Runnable() {
        @Override public void run() {
            if (rain == null) return;
            boolean done = Math.abs(rainTarget - rainVol) <= Math.abs(rainStep) + 0.0001f || rainStep == 0f;
            rainVol = done ? rainTarget : rainVol + rainStep;
            try {
                rain.setVolume(rainVol);
                if (done && rainVol <= 0f) rain.pause();
            } catch (Exception ignored) { }
            if (!done) main.postDelayed(this, 40);
        }
    };

    synchronized void stopLoop() {
        main.removeCallbacks(fader);
        if (rain != null) { try { rain.stop(); rain.release(); } catch (Exception ignored) { } rain = null; }
    }

    /* ---- uploaded ambience (an OGG loop from the Theme manager), played with MediaPlayer ---- */
    private android.media.MediaPlayer file;
    private String filePath;
    private float fileVol = 0f, fileTarget = 0f, fileStep = 0f;

    void fileTo(final String path, final float vol, final int ms) {
        main.post(new Runnable() { @Override public void run() {
            try {
                if (path != null && !path.equals(filePath)) {
                    releaseFile();
                    if (!new File(path).exists()) return;
                    file = new android.media.MediaPlayer();
                    file.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build());
                    file.setDataSource(path);
                    file.setLooping(true);
                    file.setOnErrorListener(new android.media.MediaPlayer.OnErrorListener() {   // e.g. the audio output went away
                        @Override public boolean onError(android.media.MediaPlayer mp, int what, int extra) {
                            Log.w(TAG, "ambience file error " + what + "/" + extra + ", rebuilding");
                            revive(); return true;
                        }
                    });
                    file.setVolume(0f, 0f);
                    file.prepare();
                    filePath = path; fileVol = 0f;
                }
                if (file == null) return;
                fileTarget = Math.max(0f, Math.min(1f, vol));
                fileStep = (fileTarget - fileVol) / Math.max(1, ms / 40);
                if (fileTarget > 0 && !file.isPlaying()) file.start();
                if (fileTarget > 0) watch();
                main.removeCallbacks(fileFader);
                main.post(fileFader);
            } catch (Exception e) { Log.w(TAG, "ambience file failed " + path, e); releaseFile(); }
        } });
    }

    private final Runnable fileFader = new Runnable() {
        @Override public void run() {
            if (file == null) return;
            boolean done = Math.abs(fileTarget - fileVol) <= Math.abs(fileStep) + 0.0001f || fileStep == 0f;
            fileVol = done ? fileTarget : fileVol + fileStep;
            try {
                file.setVolume(fileVol, fileVol);
                if (done && fileVol <= 0f && file.isPlaying()) file.pause();
            } catch (Exception ignored) { }
            if (!done) main.postDelayed(this, 40);
        }
    };

    private void releaseFile() {
        main.removeCallbacks(fileFader);
        if (file != null) { try { file.release(); } catch (Exception ignored) { } file = null; }
        filePath = null;
    }

    /**
     * For the menu's equaliser: n levels (0..1, low to high pitch) of the rain loop at the point being played,
     * measured with Goertzel filters on 512 samples and scaled by the current fade. "" when the rain isn't
     * playing (an uploaded sound or nothing), and the menu then follows its own volume instead.
     */
    String levels(int n) {
        AudioTrack r = rain; byte[] pcm = loopPcm; int sr = loopRate; float v = rainVol;
        if (r == null || pcm == null || sr <= 0 || v <= 0.001f || n <= 0 || n > 64) return "";
        try { if (r.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) return ""; } catch (Exception e) { return ""; }
        int total = pcm.length / 2; if (total < 1024) return "";
        int pos;
        try { pos = (int) ((r.getPlaybackHeadPosition() & 0xffffffffL) % total); } catch (Exception e) { return ""; }
        final int N = 512;
        StringBuilder out = new StringBuilder();
        double gainScale = Math.min(1.0, 0.35 + v * 2.0);   // louder ambience, taller bars
        for (int b = 0; b < n; b++) {
            double f = 90 * Math.pow(7500 / 90.0, n == 1 ? 0 : b / (double) (n - 1));
            double k = 2 * Math.cos(2 * Math.PI * f / sr), s1 = 0, s2 = 0;
            for (int i = 0; i < N; i++) {
                int idx = ((pos + i) % total) * 2;
                short x = (short) ((pcm[idx] & 0xff) | (pcm[idx + 1] << 8));
                double w = x / 32768.0 * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1)));
                double s0 = w + k * s1 - s2; s2 = s1; s1 = s0;
            }
            double mag = Math.sqrt(Math.max(0, s1 * s1 + s2 * s2 - k * s1 * s2)) / (N / 4.0);
            // the rain sits around -48 to -28 dB per band (measured on the built-in loop); higher bands a little
            // quieter, so they get a small lift and every bar moves
            double db = 20 * Math.log10(mag + 1e-6) + 6.0 * b / Math.max(1, n - 1);
            double lvl = Math.max(0, Math.min(1, (db + 50) / 24)) * gainScale;
            if (b > 0) out.append(',');
            out.append(Math.round(lvl * 1000) / 1000.0);
        }
        return out.toString();
    }

    /**
     * After standby (or a long screensaver) some TVs switch off or re-route their audio output, e.g. Sony with a
     * home theater on HDMI ARC: players made before then stay "playing" but are silent. Rebuild them, at the same
     * volume they were heading for, so the menu's next fade-in is heard.
     */
    void revive() {
        main.post(new Runnable() { @Override public void run() {
            lastRevive = System.currentTimeMillis();
            float rt = rainTarget, ft = fileTarget;
            String fp = filePath;
            synchronized (NativeSound.this) {
                if (loopPcm != null) loadLoop(loopPcm, loopRate);
            }
            rainTarget = 0f;
            if (rt > 0f) loopTo(rt, 600);
            if (fp != null) { releaseFile(); fileTo(fp, ft, 600); }
            Log.i(TAG, "ambience players rebuilt");
        } });
    }

    /** Rebuild the players a little later (the TV's sound output often comes back a few seconds after the picture). */
    private final Runnable reviveRun = new Runnable() { @Override public void run() { revive(); } };
    void reviveSoon(long ms) { main.removeCallbacks(reviveRun); main.postDelayed(reviveRun, ms); }

    /*
     * Self-check while an ambience should be playing (every 5 s): if the player has stopped moving for 10 s,
     * the TV's sound output went away under it (standby, a long film in another app, the soundbar switching
     * back from Netflix's Dolby sound). Rebuild it. At most once every 20 s, and only while something should be heard.
     */
    private long lastHead = -1, lastFilePos = -1, lastRevive = 0;
    private int stuck = 0;
    private boolean watching;
    private void watch() { if (!watching) { watching = true; stuck = 0; lastHead = -1; lastFilePos = -1; main.postDelayed(watchdog, 5000); } }
    private final Runnable watchdog = new Runnable() {
        @Override public void run() {
            boolean rainOn = rain != null && rainTarget > 0f, fileOn = file != null && fileTarget > 0f;
            if (!rainOn && !fileOn) { watching = false; return; }
            boolean moving = true;
            try {
                if (rainOn) {
                    long h = rain.getPlayState() == AudioTrack.PLAYSTATE_PLAYING ? (rain.getPlaybackHeadPosition() & 0xffffffffL) : -2;
                    if (h == -2 || h == lastHead) moving = false;
                    lastHead = h;
                }
                if (fileOn) {
                    long fp = file.isPlaying() ? file.getCurrentPosition() : -2;
                    if (fp == -2 || fp == lastFilePos) moving = false;
                    lastFilePos = fp;
                }
            } catch (Exception e) { moving = false; }
            stuck = moving ? 0 : stuck + 1;
            if (stuck >= 2 && System.currentTimeMillis() - lastRevive > 20_000) {
                Log.i(TAG, "ambience stopped moving, rebuilding");
                stuck = 0; lastHead = -1; lastFilePos = -1; revive();
            }
            main.postDelayed(this, 5000);
        }
    };

    void release() {
        main.post(new Runnable() { @Override public void run() { releaseFile(); } });
        stopLoop();
        try { pool.release(); } catch (Exception ignored) { }
    }
}
