package sg.round2.tvhome;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.media.tv.TvContract;
import android.media.tv.TvInputInfo;
import android.media.tv.TvInputManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Round2 Home: a TV home screen that shows the Round2 menu (assets/index.html)
 * and lets it open apps and HDMI inputs through the R2Android bridge.
 */
public class MainActivity extends Activity {

    private WebView web;
    private String pendingRoom;
    private boolean pageReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        web = new WebView(this);
        web.setBackgroundColor(0xFF06050A);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);   // muted background video
        s.setAllowFileAccess(false);
        s.setTextZoom(100);                // ignore the TV's large-text setting; the menu scales itself
        s.setUseWideViewPort(true);
        s.setLoadWithOverviewMode(true);
        web.setInitialScale(0);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                return true;   // the home screen never navigates away
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                pageReady = true;
                applyPendingRoom();
            }
        });
        web.setWebChromeClient(new WebChromeClient());
        web.addJavascriptInterface(new Bridge(this), "R2Android");
        web.setFocusable(true);
        web.setFocusableInTouchMode(true);
        setContentView(web);
        pendingRoom = getIntent().getStringExtra("room");
        web.loadUrl("file:///android_asset/index.html");
        web.requestFocus(View.FOCUS_DOWN);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        // Set this TV's room from the computer:
        // adb shell am start -n sg.round2.tvhome/.MainActivity --es room vale
        String room = intent.getStringExtra("room");
        if (room != null) { pendingRoom = room; applyPendingRoom(); }
    }

    private void applyPendingRoom() {
        if (!pageReady || pendingRoom == null) return;
        String safe = pendingRoom.replaceAll("[^a-z]", "");
        pendingRoom = null;
        web.evaluateJavascript("window.r2SetRoom && window.r2SetRoom('" + safe + "')", null);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (web != null) {
            web.onResume();
            web.requestFocus();
            web.evaluateJavascript("window.r2Resume && window.r2Resume()", null);
        }
    }

    @Override
    protected void onPause() {
        if (web != null) web.onPause();   // stops the background video while an app is open
        super.onPause();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (e.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (e.getAction() == KeyEvent.ACTION_UP) {
                web.evaluateJavascript("window.r2Back && window.r2Back()", null);
            }
            return true;   // a home screen never closes on Back
        }
        return super.dispatchKeyEvent(e);
    }

    /** Methods the web menu can call as window.R2Android.* */
    static final class Bridge {
        private final Activity act;

        Bridge(Activity act) { this.act = act; }

        @JavascriptInterface
        public String launch(String pkg) {
            if (pkg == null || pkg.isEmpty()) return "No app is set for this item.";
            PackageManager pm = act.getPackageManager();
            Intent i = pm.getLeanbackLaunchIntentForPackage(pkg);
            if (i == null) i = pm.getLaunchIntentForPackage(pkg);
            if (i == null) return "This app isn't installed on this TV.";
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            try { act.startActivity(i); return "ok"; }
            catch (ActivityNotFoundException ex) { return "The TV could not open this app."; }
        }

        /** HDMI and other hardware inputs, as [{id, name}]. */
        @JavascriptInterface
        public String inputs() {
            JSONArray out = new JSONArray();
            if (Build.VERSION.SDK_INT < 21) return out.toString();
            try {
                TvInputManager tim = (TvInputManager) act.getSystemService(Context.TV_INPUT_SERVICE);
                if (tim == null) return out.toString();
                List<TvInputInfo> list = new ArrayList<>(tim.getTvInputList());
                List<JSONObject> rows = new ArrayList<>();
                for (TvInputInfo in : list) {
                    if (!in.isPassthroughInput()) continue;
                    if (in.getParentId() != null) continue;   // skip HDMI-CEC child devices
                    String name = String.valueOf(in.loadLabel(act));
                    JSONObject o = new JSONObject();
                    o.put("id", in.getId());
                    o.put("name", name);
                    o.put("type", in.getType());
                    rows.add(o);
                }
                Collections.sort(rows, new Comparator<JSONObject>() {
                    @Override public int compare(JSONObject a, JSONObject b) {
                        return a.optString("name").compareToIgnoreCase(b.optString("name"));
                    }
                });
                for (JSONObject o : rows) out.put(o);
            } catch (Exception ignored) { }
            return out.toString();
        }

        @JavascriptInterface
        public String openInput(String inputId) {
            if (Build.VERSION.SDK_INT < 21) return "This TV can't switch inputs from apps.";
            try {
                Uri uri = TvContract.buildChannelUriForPassthroughInput(inputId);
                Intent i = new Intent(Intent.ACTION_VIEW, uri);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                act.startActivity(i);
                return "ok";
            } catch (Exception ex) {
                return "The TV could not switch to this input.";
            }
        }

        /** Launchable apps for the "More apps" grid, as [{name, android}]. */
        @JavascriptInterface
        public String apps() {
            JSONArray out = new JSONArray();
            try {
                PackageManager pm = act.getPackageManager();
                Set<String> seen = new HashSet<>();
                seen.add(act.getPackageName());
                List<ResolveInfo> found = new ArrayList<>();
                Intent tv = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LEANBACK_LAUNCHER);
                found.addAll(pm.queryIntentActivities(tv, 0));
                List<JSONObject> rows = new ArrayList<>();
                for (ResolveInfo r : found) {
                    String pkg = r.activityInfo.packageName;
                    if (seen.contains(pkg)) continue;
                    ApplicationInfo ai = r.activityInfo.applicationInfo;
                    if (ai != null && !ai.enabled) continue;
                    seen.add(pkg);
                    JSONObject o = new JSONObject();
                    o.put("name", String.valueOf(r.loadLabel(pm)));
                    o.put("android", pkg);
                    rows.add(o);
                }
                Collections.sort(rows, new Comparator<JSONObject>() {
                    @Override public int compare(JSONObject a, JSONObject b) {
                        return a.optString("name").compareToIgnoreCase(b.optString("name"));
                    }
                });
                for (JSONObject o : rows) out.put(o);
            } catch (Exception ignored) { }
            return out.toString();
        }
    }
}
