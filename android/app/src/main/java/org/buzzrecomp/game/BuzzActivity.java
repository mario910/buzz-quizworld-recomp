package org.buzzrecomp.game;

import android.app.AlertDialog;
import android.content.Intent;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Build;
import android.os.Process;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.SurfaceHolder;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.Toast;
import android.widget.TextView;
import org.json.JSONObject;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.UUID;

public class BuzzActivity extends SDLActivity {
    private static final int IMPORT_DRIVER = 41;
    private boolean nativeReady, recovering, surfaceAvailable, activityResumed, windowFocused;
    private int player, held;
    private FrameLayout panel;
    private LinearLayout buzzer;
    private Button playerButton;
    private final Button[] buttons = new Button[5];
    private static native void nativeConfigure(String hooks, String dir, String library, String status, String pending);
    private static native void nativeButtons(int player, int held, boolean cancel);
    private static native int nativePlayers();
    private static native long nativeFrames();
    private static native void nativeSurfaceReady(boolean ready);
    private static native void nativeMenu();
    private static native boolean nativeDiagnosticCpuMask(int mask);
    private static native boolean nativeDiagnosticSamples(String file);
    private static native void nativeDiagnosticAudioMode(int mode);
    private static native void nativeDiagnosticSkipIntro();
    private SharedPreferences preferences() { return getSharedPreferences("android", MODE_PRIVATE); }
    private File pending() { return new File(getFilesDir(), "gpu-driver.pending"); }
    private void updateNativeVisibility() {
        if (nativeReady) nativeSurfaceReady(surfaceAvailable && activityResumed && windowFocused);
    }

    // Opt-in, debug-only device runs must end even if SDL/the guest stops
    // making progress. Do not call JNI, the UI thread or log/file flushes on
    // this deadline path: any of those may be the subsystem being diagnosed.
    private void armDiagnosticTimeout() {
        if ((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) == 0) return;
        int seconds = getIntent().getIntExtra("buzz_test_seconds", 0);
        if (seconds == 0) return; // Ordinary play has no watchdog.
        // Up to 5 minutes: reaching the studio with scripted input takes ~2.
        if (seconds < 5 || seconds > 300) throw new IllegalArgumentException("buzz_test_seconds must be 5..300");
        final int ownPid = Process.myPid();
        final long deadline = android.os.SystemClock.elapsedRealtime() + seconds * 1000L;
        Thread watchdog = new Thread(() -> {
            long remaining;
            while ((remaining = deadline - android.os.SystemClock.elapsedRealtime()) > 0)
                android.os.SystemClock.sleep(Math.min(remaining, 500));
            Process.killProcess(ownPid);
        }, "BuzzTestDeadline");
        watchdog.setDaemon(true);
        watchdog.start();
        android.util.Log.i("Buzz", "Diagnostic process deadline armed: " + seconds + " seconds");
    }

    @Override public void loadLibraries() {
        super.loadLibraries();
        nativeReady = true;
        if ((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
            if (getIntent().getBooleanExtra("buzz_test_timeline", false)) {
                if (getIntent().getIntExtra("buzz_test_seconds", 0) < 50)
                    throw new IllegalArgumentException("Timeline diagnostic requires a bounded run of at least 50 seconds");
                nativeSetenv("PS3_FTRACE", "32:10:" + new File(getFilesDir(), "diagnostic-timeline.txt").getAbsolutePath());
                nativeSetenv("PS3_FTRACE_MIN_US", "10");
                nativeSetenv("PS3_FTRACE_CAP", "500000");
            }
            if (getIntent().hasExtra("buzz_test_job_workers")) {
                int workers = getIntent().getIntExtra("buzz_test_job_workers", 0);
                if (getIntent().getIntExtra("buzz_test_seconds", 0) == 0 || workers < 1 || workers > 4)
                    throw new IllegalArgumentException("Worker diagnostic requires a bounded run and 1..4 workers");
                nativeSetenv("SPURS_JOB_LIST_WORKERS", Integer.toString(workers));
                android.util.Log.i("Buzz", "Diagnostic job-list workers: " + workers);
            }
            if (getIntent().hasExtra("buzz_test_idle_ms")) {
                int idleMs = getIntent().getIntExtra("buzz_test_idle_ms", -1);
                if (getIntent().getIntExtra("buzz_test_seconds", 0) == 0 || idleMs < 0 || idleMs > 16)
                    throw new IllegalArgumentException("FIFO idle diagnostic requires a bounded run and 0..16 ms");
                nativeSetenv("BUZZ_GCM_IDLE_MS", Integer.toString(idleMs));
                android.util.Log.i("Buzz", "Diagnostic FIFO idle timeout: " + idleMs + " ms");
            }
            // Scripted buzzer presses: the shared virtual-buzzer mailbox, read
            // from files/test-input.txt ("<sequence>:<hex mask>\n"; write it
            // with run-as), so a bounded run can reach the studio unattended.
            if (getIntent().getBooleanExtra("buzz_test_input_file", false)) {
                if (getIntent().getIntExtra("buzz_test_seconds", 0) == 0)
                    throw new IllegalArgumentException("Input mailbox diagnostic requires a bounded run");
                File mailbox = new File(getFilesDir(), "test-input.txt");
                try (java.io.FileOutputStream out = new java.io.FileOutputStream(mailbox)) {
                    out.write("0:0\n".getBytes(java.nio.charset.StandardCharsets.US_ASCII));
                } catch (java.io.IOException error) {
                    throw new IllegalStateException("Cannot create the input mailbox", error);
                }
                nativeSetenv("BUZZ_TEST_INPUT_FILE", mailbox.getAbsolutePath());
            }
            // Further diagnostic environment, "NAME=value;NAME=value", applied
            // before the runtime caches its options (e.g. SPU_INTERP_STATS=1).
            String env = getIntent().getStringExtra("buzz_test_env");
            if (env != null && !env.isEmpty()) {
                if (getIntent().getIntExtra("buzz_test_seconds", 0) == 0)
                    throw new IllegalArgumentException("Diagnostic environment requires a bounded run");
                for (String pair : env.split(";")) {
                    int eq = pair.indexOf('=');
                    if (eq <= 0) throw new IllegalArgumentException("buzz_test_env expects NAME=value pairs");
                    nativeSetenv(pair.substring(0, eq), pair.substring(eq + 1));
                    android.util.Log.i("Buzz", "Diagnostic environment: " + pair);
                }
            }
            if (getIntent().getBooleanExtra("buzz_test_skip_intro", false)) {
                if (getIntent().getIntExtra("buzz_test_seconds", 0) == 0)
                    throw new IllegalArgumentException("Intro diagnostic requires a bounded run");
                nativeDiagnosticSkipIntro();
            }
            if (getIntent().hasExtra("buzz_test_audio_mode")) {
                int audioMode = getIntent().getIntExtra("buzz_test_audio_mode", -1);
                if (getIntent().getIntExtra("buzz_test_seconds", 0) == 0 || audioMode < 0 || audioMode > 2)
                    throw new IllegalArgumentException("Audio diagnostic mode requires a bounded run and mode 0..2");
                nativeDiagnosticAudioMode(audioMode);
            }
            int mask = getIntent().getIntExtra("buzz_test_cpu_mask", 0);
            if (mask != 0 && (getIntent().getIntExtra("buzz_test_seconds", 0) == 0 ||
                    !nativeDiagnosticCpuMask(mask)))
                throw new IllegalArgumentException("Diagnostic CPU mask requires a bounded run and available CPUs");
            if (getIntent().getBooleanExtra("buzz_test_pc_samples", false) &&
                    (getIntent().getIntExtra("buzz_test_seconds", 0) == 0 ||
                     !nativeDiagnosticSamples(new File(getFilesDir(), "diagnostic-pc-samples.bin").getAbsolutePath())))
                throw new IllegalArgumentException("Native sampling requires a bounded run and an unused profiler signal");
        }
        recovering = pending().exists();
        if (recovering) { preferences().edit().remove("driver").commit(); pending().delete(); }
        String selected = preferences().getString("driver", "");
        String directory = null, library = null;
        if (!selected.isEmpty()) try {
            File dir = new File(getFilesDir(), "drivers/" + selected);
            JSONObject meta = metadata(dir);
            library = meta.getString("libraryName");
            directory = dir.getAbsolutePath() + "/";
            pending().createNewFile();
        } catch (Exception error) {
            preferences().edit().remove("driver").commit(); directory = null; library = null;
        }
        nativeConfigure(getApplicationInfo().nativeLibraryDir, directory, library,
            new File(getFilesDir(), "gpu-driver-status.txt").getAbsolutePath(), pending().getAbsolutePath());
    }
    @Override protected SDLSurface createSDLSurface(Context context) {
        return new SDLSurface(context) {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                super.surfaceCreated(holder);
                surfaceAvailable = true; updateNativeVisibility();
            }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                super.surfaceChanged(holder, format, width, height);
                surfaceAvailable = true; updateNativeVisibility();
            }
            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                surfaceAvailable = false; updateNativeVisibility();
                super.surfaceDestroyed(holder);
            }
        };
    }
    @Override protected void onCreate(Bundle state) {
        armDiagnosticTimeout();
        super.onCreate(state);
        if (!nativeReady || mLayout == null) return;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        panel = new FrameLayout(this);
        mLayout.addView(panel, new android.view.ViewGroup.LayoutParams(-1, -1));
        LinearLayout tools = new LinearLayout(this);
        Button menu = button("Menu", 0xcc20252d);
        menu.setContentDescription("Open game menu");
        menu.setOnClickListener(v -> { release(); nativeMenu(); });
        Button gpu = button("GPU", 0xcc20252d);
        gpu.setContentDescription("GPU driver and on-screen controls");
        gpu.setOnClickListener(v -> showSettings());
        tools.addView(menu, new LinearLayout.LayoutParams(dp(76), dp(44)));
        tools.addView(gpu, new LinearLayout.LayoutParams(dp(64), dp(44)));
        TextView fps = new TextView(this); fps.setTextColor(Color.WHITE); fps.setTextSize(13);
        fps.setGravity(Gravity.CENTER); fps.setBackgroundColor(0x9920252d);
        tools.addView(fps, new LinearLayout.LayoutParams(dp(70), dp(44)));
        fps.postDelayed(new Runnable() {
            long frames = nativeFrames(), time = android.os.SystemClock.elapsedRealtime();
            public void run() {
                if (isFinishing() || isDestroyed()) return;
                long now = android.os.SystemClock.elapsedRealtime(), count = nativeFrames();
                fps.setText(String.format(java.util.Locale.ROOT, "%.1f FPS", 1000.0 * (count - frames) / Math.max(1, now - time)));
                frames = count; time = now; fps.postDelayed(this, 1000);
            }
        }, 1000);
        FrameLayout.LayoutParams top = new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.LEFT);
        top.setMargins(dp(12), dp(8), dp(12), 0); panel.addView(tools, top);
        buzzer = new LinearLayout(this); buzzer.setOrientation(LinearLayout.VERTICAL);
        playerButton = button("P1", 0xcc20252d);
        playerButton.setContentDescription("Select buzzer player");
        playerButton.setOnClickListener(v -> choosePlayer());
        buzzer.addView(playerButton, new LinearLayout.LayoutParams(dp(76), dp(36)));
        int[] colors = {0xffd62c3a, 0xffffd043, 0xff39b967, 0xffff8e28, 0xff258de3};
        String[] labels = {"BUZZ", "Yellow", "Green", "Orange", "Blue"};
        // Physical handset order differs from the USB/router bit order.
        int[] handsetOrder = {0, 4, 3, 2, 1};
        for (int i : handsetOrder) {
            final int bit = 1 << i;
            Button b = buttons[i] = button(i == 0 ? "BUZZ" : "", colors[i]);
            b.setContentDescription(labels[i] + " buzzer button");
            b.setOnTouchListener((view, event) -> {
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        held |= bit; view.setAlpha(1); view.setPressed(true);
                        nativeButtons(player, held, false); return true;
                    case MotionEvent.ACTION_UP:
                        held &= ~bit; view.setAlpha(.78f); view.setPressed(false);
                        nativeButtons(player, held, false); view.performClick(); return true;
                    case MotionEvent.ACTION_CANCEL:
                        release(); return true;
                    default: return true;
                }
            });
            b.setAlpha(.78f);
            LinearLayout.LayoutParams row = new LinearLayout.LayoutParams(dp(76), dp(i == 0 ? 62 : 42));
            row.topMargin = dp(4); buzzer.addView(b, row);
        }
        FrameLayout.LayoutParams bottom = new FrameLayout.LayoutParams(-2, -2, Gravity.RIGHT | Gravity.BOTTOM);
        bottom.setMargins(0, 0, dp(12), dp(10)); panel.addView(buzzer, bottom);
        buzzer.setVisibility(preferences().getBoolean("touch", true) ? View.VISIBLE : View.GONE);
        if (recovering) panel.post(() -> new AlertDialog.Builder(this).setTitle("GPU driver")
            .setMessage("The previous launch did not display a frame. The system driver has been restored.")
            .setPositiveButton("OK", null).show());
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private Button button(String text, int color) {
        Button b = new Button(this); b.setText(text); b.setTextColor(Color.WHITE);
        b.setTextSize(13); b.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        b.setPadding(0, 0, 0, 0); b.setMinWidth(0); b.setMinHeight(0);
        GradientDrawable background = new GradientDrawable(); background.setColor(color);
        background.setCornerRadius(dp(14)); background.setStroke(dp(1), 0x88ffffff);
        b.setBackground(background); return b;
    }
    private void release() {
        held = 0;
        if (nativeReady) nativeButtons(player, 0, true);
        for (Button b : buttons) if (b != null) { b.setAlpha(.78f); b.setPressed(false); }
        // A neutral state re-arms the input gate without carrying old taps.
        if (nativeReady) nativeButtons(player, 0, false);
    }
    private void choosePlayer() {
        release();
        int count = Math.max(1, Math.min(8, nativePlayers()));
        String[] names = new String[count];
        for (int i = 0; i < count; i++) names[i] = "Player " + (i + 1);
        new AlertDialog.Builder(this).setTitle("On-screen buzzer").setSingleChoiceItems(names, player, (dialog, which) -> {
            release(); player = which; playerButton.setText("P" + (player + 1)); dialog.dismiss();
        }).show();
    }
    private void showSettings() {
        release();
        String status = "System driver";
        try { status = new String(Files.readAllBytes(new File(getFilesDir(), "gpu-driver-status.txt").toPath()), StandardCharsets.UTF_8); }
        catch (IOException ignored) {}
        final String[] actions = {"Import driver from ZIP…", "Use system driver", "Show / hide buzzer"};
        new AlertDialog.Builder(this).setTitle("GPU / controls").setMessage(status)
            .setPositiveButton("Close", null).setNeutralButton("Options", (dialog, which) ->
                new AlertDialog.Builder(this).setItems(actions, (options, index) -> {
                    if (index == 0) startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT)
                        .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
                        .putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                            android.net.Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADownload")), IMPORT_DRIVER);
                    else if (index == 1) { preferences().edit().remove("driver").commit(); restartNotice(); }
                    else { boolean show = buzzer.getVisibility() != View.VISIBLE;
                        release(); buzzer.setVisibility(show ? View.VISIBLE : View.GONE);
                        preferences().edit().putBoolean("touch", show).apply(); }
                }).show()).show();
    }
    private static JSONObject metadata(File dir) throws Exception {
        File json = new File(dir, "meta.json");
        if (json.length() > 65536) throw new IOException("meta.json is too large");
        JSONObject meta = new JSONObject(new String(Files.readAllBytes(json.toPath()), StandardCharsets.UTF_8));
        String library = meta.getString("libraryName");
        if (meta.getInt("schemaVersion") != 1 || meta.getInt("minApi") > Build.VERSION.SDK_INT ||
            !library.matches("[A-Za-z0-9_+.-]{1,157}\\.so") || !new File(dir, library).isFile())
            throw new IOException("Unsupported driver package");
        DriverArchive.validateElf(new File(dir, library));
        meta.getString("name"); return meta;
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != IMPORT_DRIVER || result != RESULT_OK || data == null || data.getData() == null) return;
        final android.net.Uri uri = data.getData();
        Toast.makeText(this, "Importing driver…", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            File root = new File(getFilesDir(), "drivers"); root.mkdirs();
            String id = UUID.randomUUID().toString(); File dir = new File(root, id);
            try (InputStream input = getContentResolver().openInputStream(uri)) {
                if (input == null) throw new IOException("Cannot open file");
                DriverArchive.extract(input, dir); JSONObject meta = metadata(dir);
                preferences().edit().putString("driver", id).commit();
                runOnUiThread(() -> restartNotice());
            } catch (Exception error) {
                DriverArchive.remove(dir);
                runOnUiThread(() -> new AlertDialog.Builder(this).setTitle("Cannot import driver")
                    .setMessage(error.getMessage()).setPositiveButton("OK", null).show());
            }
        }, "DriverImport").start();
    }
    private void restartNotice() {
        new AlertDialog.Builder(this).setTitle("Driver settings saved")
            .setMessage("The change will take effect after restarting the game.")
            .setPositiveButton("Close game", (dialog, which) -> { finishAndRemoveTask(); Process.killProcess(Process.myPid()); })
            .setNegativeButton("Later", null).show();
    }
    @Override protected void onPause() {
        activityResumed = false;
        updateNativeVisibility();
        release(); super.onPause();
    }
    @Override protected void onResume() {
        super.onResume();
        activityResumed = true;
        windowFocused = hasWindowFocus();
        updateNativeVisibility();
    }
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        windowFocused = focus;
        updateNativeVisibility();
        if (!focus) release();
        else getWindow().getDecorView().setSystemUiVisibility(5894);
    }
    @Override public void onBackPressed() { if (nativeReady) { release(); nativeMenu(); } else super.onBackPressed(); }
}
