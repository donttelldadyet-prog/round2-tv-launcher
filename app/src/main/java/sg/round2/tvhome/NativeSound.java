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
                    file.setVolume(0f, 0f);
                    file.prepare();
                    filePath = path; fileVol = 0f;
                }
                if (file == null) return;
                fileTarget = Math.max(0f, Math.min(1f, vol));
                fileStep = (fileTarget - fileVol) / Math.max(1, ms / 40);
                if (fileTarget > 0 && !file.isPlaying()) file.start();
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

    void release() {
        main.post(new Runnable() { @Override public void run() { releaseFile(); } });
        stopLoop();
        try { pool.release(); } catch (Exception ignored) { }
    }
}
