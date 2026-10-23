package sg.round2.tvhome;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.MediaPlayer;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * Plays the menu's background loop behind the (transparent) menu WebView.
 *
 * Uses a SurfaceView so the TV's display hardware shows the video on its own plane and
 * merges it with the menu itself. A TextureView (the previous version) made the GPU copy
 * every video frame into the app's picture, which slowed the video whenever the menu
 * animated on the TCL's Realtek chip.
 */
final class NativeBackground implements SurfaceHolder.Callback {

    private final Context ctx;
    final SurfaceView view;
    private MediaPlayer player;
    private boolean hasSurface;
    private String current;      // asset path that should be playing
    private boolean paused;

    NativeBackground(Context ctx) {
        this.ctx = ctx;
        view = new SurfaceView(ctx);
        view.getHolder().addCallback(this);
    }

    /** Play an asset like "media/summit-kai-right.mp4", or an absolute file path, on a loop. */
    void play(String assetPath) {
        if (assetPath == null) return;
        if (assetPath.equals(current) && player != null) { if (!paused) safeStart(); return; }
        current = assetPath;
        if (hasSurface) start();
    }

    void stop() { current = null; release(); }

    void pause() { paused = true; try { if (player != null && player.isPlaying()) player.pause(); } catch (Exception ignored) { } }

    void resume() { paused = false; safeStart(); }

    /** Kept for the menu's bridge; the hardware video plane is not scaled during transitions. */
    void zoom(float from, float to, int ms, float px, float py, boolean easeIn) { }

    private void safeStart() { try { if (player != null) player.start(); } catch (Exception ignored) { } }

    private void start() {
        release();
        if (current == null) return;
        try {
            final MediaPlayer mp = new MediaPlayer();
            if (current.startsWith("/")) {          // a downloaded event theme in the app's storage
                java.io.FileInputStream in = new java.io.FileInputStream(current);
                mp.setDataSource(in.getFD());
                in.close();
            } else {                                // bundled in the app (stored uncompressed)
                AssetFileDescriptor afd = ctx.getAssets().openFd(current);
                mp.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
                afd.close();
            }
            mp.setDisplay(view.getHolder());
            mp.setLooping(true);
            mp.setVolume(0f, 0f);
            mp.setOnPreparedListener(new MediaPlayer.OnPreparedListener() {
                @Override public void onPrepared(MediaPlayer p) { if (p == player && !paused) p.start(); }
            });
            player = mp;
            mp.prepareAsync();
        } catch (Exception e) {
            release();
        }
    }

    private void release() {
        if (player != null) {
            try { player.release(); } catch (Exception ignored) { }
            player = null;
        }
    }

    @Override public void surfaceCreated(SurfaceHolder h) { hasSurface = true; if (current != null) start(); }

    @Override public void surfaceChanged(SurfaceHolder h, int format, int w, int hgt) { }

    @Override public void surfaceDestroyed(SurfaceHolder h) { hasSurface = false; release(); }
}
