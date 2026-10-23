package sg.round2.tvhome;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Date;

/** Writes any crash to the TV's log and to a file, so 'round2tv.ps1 diag' can show what went wrong. */
final class CrashLog {
    private static boolean installed;

    static synchronized void install(Context ctx) {
        if (installed) return;
        installed = true;
        final File f = new File(ctx.getApplicationContext().getFilesDir(), "last-crash.txt");
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override public void uncaughtException(Thread t, Throwable e) {
                Log.e("R2Home", "CRASH in " + t.getName(), e);
                try (PrintWriter w = new PrintWriter(new FileWriter(f))) {
                    w.println(new Date() + " in " + t.getName());
                    e.printStackTrace(w);
                } catch (Exception ignored) { }
                if (prev != null) prev.uncaughtException(t, e);
            }
        });
    }
}
