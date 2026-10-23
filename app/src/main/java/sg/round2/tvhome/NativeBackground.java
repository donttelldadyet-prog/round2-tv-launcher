package sg.round2.tvhome;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.graphics.SurfaceTexture;
import android.media.MediaPlayer;
import android.view.Surface;
import android.view.TextureView;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.Interpolator;
import android.view.animation.PathInterpolator;

/**
 * Plays the menu's background loop with the TV's hardware video decoder, behind the
 * (transparent) menu WebView. Decoding video inside the WebView stuttered on the TCL's
 * Realtek chip; this path is what native launchers use. A TextureView is used rather than
 * a SurfaceView so the launch transitions can zoom it.
 */
final class NativeBackground implements TextureView.SurfaceTextureListener {

    private final Context ctx;
    final TextureView view;
    private MediaPlayer player;
    private Surface surface;
    private String current;      // asset path playing now
    private String pending;      // asset path to play once the surface exists
    private boolean paused;

    NativeBackground(Context ctx) {
        this.ctx = ctx;
        view = new TextureView(ctx);
        view.setSurfaceTextureListener(this);
        view.setOpaque(true);
        view.setAlpha(0f);
    }

    /** Play an asset like "media/summit-kai-right.mp4" on a loop; fades in when ready. */
    void play(String assetPath) {
        if (assetPath == null || assetPath.equals(current)) { if (player != null && !paused) player.start(); return; }
        pending = assetPath;
        if (surface != null) startPending();
    }

    void stop() {
        current = null; pending = null;
        view.animate().alpha(0f).setDuration(200).start();
        release();
    }

    void pause() { paused = true; try { if (player != null && player.isPlaying()) player.pause(); } catch (Exception ignored) { } }

    void resume() { paused = false; try { if (player != null) player.start(); } catch (Exception ignored) { } }

    /**
     * Zoom the video toward a point (fractions of the screen) for the launch transitions.
     * from < 0 starts from the current scale.
     */
    void zoom(float from, float to, int ms, float px, float py, boolean easeIn) {
        view.animate().cancel();
        view.setPivotX(view.getWidth() * px);
        view.setPivotY(view.getHeight() * py);
        if (from >= 0) { view.setScaleX(from); view.setScaleY(from); }
        Interpolator in = easeIn ? new PathInterpolator(0.55f, 0f, 0.85f, 0.35f) : new DecelerateInterpolator(2f);
        view.animate().scaleX(to).scaleY(to).setDuration(ms).setInterpolator(in).start();
    }

    private void startPending() {
        final String path = pending;
        pending = null;
        release();
        try {
            AssetFileDescriptor afd = ctx.getAssets().openFd(path);
            final MediaPlayer mp = new MediaPlayer();
            mp.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
            afd.close();
            mp.setSurface(surface);
            mp.setLooping(true);
            mp.setVolume(0f, 0f);
            mp.setOnPreparedListener(new MediaPlayer.OnPreparedListener() {
                @Override public void onPrepared(MediaPlayer p) {
                    if (p != player) return;
                    if (!paused) p.start();
                    view.animate().alpha(1f).setDuration(220).start();
                }
            });
            player = mp;
            current = path;
            view.setAlpha(0f);
            mp.prepareAsync();
        } catch (Exception e) {
            current = null;
            release();
        }
    }

    private void release() {
        if (player != null) {
            try { player.release(); } catch (Exception ignored) { }
            player = null;
        }
    }

    @Override public void onSurfaceTextureAvailable(SurfaceTexture st, int w, int h) {
        surface = new Surface(st);
        if (pending == null && current != null) { pending = current; current = null; }
        if (pending != null) startPending();
    }

    @Override public void onSurfaceTextureSizeChanged(SurfaceTexture st, int w, int h) { }

    @Override public boolean onSurfaceTextureDestroyed(SurfaceTexture st) {
        release();
        if (surface != null) { surface.release(); surface = null; }
        return true;
    }

    @Override public void onSurfaceTextureUpdated(SurfaceTexture st) { }
}
