package dev.daorecompandroid.duo;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.input.InputManager;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.List;

/**
 * The game's activity (task 4.2).
 *
 * <p>Named {@code GameActivity} after the plan's task, and deliberately <em>not</em>
 * {@code androidx.games:games-activity}'s class of the same name - see
 * {@link GameSurfaceView} for why that library is not used.
 *
 * <p>At this phase the activity's job is narrow: go fullscreen, create the surface,
 * start the native runtime, and show what the Vulkan device turned out to be. The
 * device report on screen is the point of the phase - it is the proof that the renderer
 * comes up inside a real APK, not only from a shell binary pushed to
 * {@code /data/local/tmp}.
 */
public final class GameActivity extends Activity {

    private static final String TAG = "Warden";

    /** Resumed: DeckService's answer to the deck, which never pulls the game over Home. */
    static volatile boolean inFront;

    private GameSurfaceView surfaceView;
    /** PLAN 11.1: Debugging Mode on for this run (Options, from the start). */
    private boolean debugMode;
    private TextView report;
    /** The scroller around the report, so its width can be set after layout. */
    private ScrollView reportHost;
    private ControllerManager controllers;
    private boolean runtimeStarted;
    /**
     * Guest code has started. From then on the app ends by ending the process: the
     * title's threads cannot be stopped from outside, and the runtime cannot be started
     * again in the same process (task 4.9).
     */
    private boolean guestBooted;
    /**
     * This launch starts the game: the default, so a launch from the icon plays. With
     * {@code --ez boot false} on the intent the app stays at the bring-up report
     * ({@code tools/appbringup/run.sh}), as every launch did until the game ran.
     */
    private boolean bootWanted;
    /**
     * The app opened on its settings, before the game (the owner, 2026-10-30): a launch
     * from the icon - no {@code boot} extra - shows the options panel with Launch Dragon
     * Age and the disc image's and the title update's files at its top, and the game
     * starts when Launch is chosen. The test scripts' {@code --ez boot true} starts it at
     * once, as before.
     */
    private boolean preLaunch;
    /** PLAN 8: the second screen's deck, opened as the game starts. */
    private final Deck deck = new Deck();
    /** The "Performance tab/overlay" option's main-screen half: shown when no deck is. */
    private PerfOverlay perfOverlay;
    /** PLAN 10.17: the on-screen controller, shown while its option is on and the game plays. */
    private TouchControls touchControls;
    /** PLAN 9.20: photo mode's hints and its photos saved to Downloads. */
    private PhotoOverlay photoOverlay;
    /** What the last file chosen in the pre-launch settings turned out to be. */
    private String pickNote = "";
    /** The title update in place (its file's name), or null; looked for again after a pick. */
    private GameData.TitleUpdate titleUpdate;
    /** The boot is waiting for the surface; not scheduled twice. */
    private boolean bootPending;
    /** The app is ending; the exit poll and Back are done. */
    private boolean ending;
    /** Polls native extraction progress on the main thread while it runs. */
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean extractionPolling;
    // PLAN 12.5: the making of the game's code - started this run (once after a failure),
    // when, and whether the panel is being refreshed with its progress.
    private long gameCodeStarted;
    private boolean gameCodePolling;
    /** Everything in the overlay above the live pad state, which is re-rendered. */
    private String padReportHeader = "";
    /** PLAN 9.2: the display's HDR10 peak in nits, 0 when it takes no HDR10. */
    private float hdrPeakNits;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // PLAN 10.7: how the last session ended, for the launch screen, and this one's tag.
        lastExit = LastExit.check(this);
        LastExit.mark(this, "pre-launch");

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        surfaceView = new GameSurfaceView(this);

        // The report overlay is scaffolding for this phase. It goes away once there is
        // a frame to look at instead; until then, reading it off the handheld's screen
        // beats reading logcat on another machine.
        report = new TextView(this);
        report.setTextColor(0xFFC9A227);
        report.setBackgroundColor(0xC014110F);
        report.setTextSize(11f);
        report.setPadding(24, 24, 24, 24);
        report.setTypeface(android.graphics.Typeface.MONOSPACE);
        report.setText("starting native runtime…");

        FrameLayout root = new FrameLayout(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        // The overlay is scrollable and occupies the left half.
        //
        // It was WRAP_CONTENT in both directions, which worked while the report was
        // short. It is now some seventy lines, so it ran past the bottom of the panel
        // and the end of it - the kernel import state, which is the most interesting
        // part - could not be read at all.
        //
        // Half the width rather than all of it for two reasons: the right-hand side
        // shows what the renderer is presenting, which is the point of the app; and
        // tools/appbringup/sample_screen.py samples the right-hand columns to check
        // that output is visible, so an overlay across the whole panel would make that
        // check read its own background.
        ScrollView reportScroll = new ScrollView(this);
        reportScroll.addView(report, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT));
        reportScroll.setScrollbarFadingEnabled(false);
        FrameLayout.LayoutParams reportParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT);
        reportParams.gravity = android.view.Gravity.TOP | android.view.Gravity.START;
        root.addView(reportScroll, reportParams);
        reportHost = reportScroll;
        // Over the game, under the options panel (added after it).
        perfOverlay = new PerfOverlay(this, getFilesDir(), root);
        // PLAN 9.20: photo mode's hints, over the game.
        photoOverlay = new PhotoOverlay(this, getFilesDir(), surfaceView, root);
        // PLAN 10.17: over the game and the numbers, under the options panel.
        touchControls = new TouchControls(this, getFilesDir(),
                () -> guestBooted && !ending && optionsPanel != null && !optionsPanel.isOpen(),
                this::openOptions);
        root.addView(touchControls, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        // PLAN 9.2: HDR10 is offered where the display takes it, up to its peak.
        android.view.Display display = getDisplay();
        android.view.Display.HdrCapabilities hdr =
                display == null ? null : display.getHdrCapabilities();
        if (hdr != null) {
            for (int type : hdr.getSupportedHdrTypes()) {
                if (type == android.view.Display.HdrCapabilities.HDR_TYPE_HDR10) {
                    hdrPeakNits = hdr.getDesiredMaxLuminance();
                }
            }
        }
        Options.hdrAvailable = hdrPeakNits > 0f;
        // PLAN 8: the second screen's deck is offered where there is one.
        Options.deckAvailable = Deck.findDisplay(this) != null;
        // PLAN 10.14: each session starts with the cheats off (the game's state is not the
        // file's after a restart).
        for (String cheat : new String[] {"cheat-immortal", "cheat-skills"}) {
            new java.io.File(getFilesDir(), cheat).delete();
        }
        // PLAN 10.1: over everything, shown while open.
        optionsPanel = new OptionsPanel(this, getFilesDir(), root, optionsHost);

        // After setContentView, not before: the insets controller comes from the decor
        // view, and PhoneWindow has not created one until a content view is set.
        // Calling this first throws a NullPointerException inside getInsetsController.
        goFullscreen();

        // Half the panel, set once the root knows how wide it is. A fixed dp value
        // would be wrong on the second screen (1240x1080) and on anything else.
        root.post(new Runnable() {
            @Override
            public void run() {
                if (reportHost == null || reportHost.getParent() == null) {
                    return;
                }
                FrameLayout.LayoutParams params =
                        (FrameLayout.LayoutParams) reportHost.getLayoutParams();
                params.width = Math.max(1, ((FrameLayout) reportHost.getParent()).getWidth() / 2);
                reportHost.setLayoutParams(params);
            }
        });

        // Vulkan device creation on the main thread, on purpose and only for now: it
        // takes a couple of hundred milliseconds, and keeping it synchronous means the
        // surface callbacks cannot arrive before the device exists. When the emulation
        // thread lands (Phase 5) this moves off the main thread and the ordering
        // becomes explicit instead of implicit.
        // BUG-104: test files a computer's run left (a route, injected presses, the DLC
        // hidden) change the game for every start until removed - said plainly.
        String tests = testFiles();
        if (!tests.isEmpty()) {
            Log.w(TAG, "TEST FILES IN PLACE: " + tests + " - not a player's game"
                    + " (bash tools/route.sh off removes a route's)");
        }
        // PLAN 11.1: Debugging Mode for this run - the header its log begins with.
        debugMode = DebugReport.onAtStart(getFilesDir());
        if (debugMode) {
            DebugReport.writeHeader(this, getFilesDir());
        }
        long began = System.nanoTime();
        runtimeStarted = NativeBridge.startup(getFilesDir().getAbsolutePath());
        long tookMs = (System.nanoTime() - began) / 1_000_000L;
        Log.i(TAG, "startup() = " + runtimeStarted + " in " + tookMs + " ms");
        if (runtimeStarted) {
            NativeBridge.displayHdr(hdrPeakNits);
        }

        StringBuilder text = new StringBuilder();
        text.append(runtimeStarted
                ? "Vulkan device created in " + tookMs + " ms\n\n"
                : "FAILED to create a Vulkan device (" + tookMs + " ms)\n\n");
        text.append(describeDisplay());
        text.append(NativeBridge.deviceReport());

        // After the runtime exists: attaching a pad goes straight through to native.
        controllers = new ControllerManager(
                (InputManager) getSystemService(Context.INPUT_SERVICE));
        controllers.start();

        // PLAN 9.12: which outputs are headphones, for the native side's mix. The
        // callback also reports the devices present when it is registered.
        if (runtimeStarted) {
            audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
            audioDevices = new AudioDeviceCallback() {
                @Override
                public void onAudioDevicesAdded(AudioDeviceInfo[] added) {
                    reportHeadphones();
                    reportMediaVolume();
                }

                @Override
                public void onAudioDevicesRemoved(AudioDeviceInfo[] removed) {
                    reportHeadphones();
                    reportMediaVolume();
                }
            };
            audioManager.registerAudioDeviceCallback(audioDevices, handler);
            if (followsMediaVolumeItself()) {
                mediaVolume = new BroadcastReceiver() {
                    @Override
                    public void onReceive(Context context, Intent intent) {
                        reportMediaVolume();
                    }
                };
                IntentFilter filter = new IntentFilter("android.media.VOLUME_CHANGED_ACTION");
                filter.addAction("android.media.STREAM_MUTE_CHANGED_ACTION");
                registerReceiver(mediaVolume, filter, Context.RECEIVER_NOT_EXPORTED);
                reportMediaVolume();
            }
        }

        padReportHeader = text.append('\n').toString();
        String pads = NativeBridge.padReport();
        report.setText(padReportHeader + pads);
        for (String line : pads.split("\n", -1)) {
            Log.i(TAG, "[report] " + line);
        }

        // From the icon (no extra) the settings come first; `--ez boot true` (the test
        // scripts) starts the game at once, `--ez boot false` keeps the report alone.
        // The game started at once from the icon from session 35 until the owner asked
        // for the settings first (2026-10-30).
        Intent intent = getIntent();
        boolean bootExtra = intent != null && intent.hasExtra("boot");
        preLaunch = !bootExtra;
        bootWanted = bootExtra && intent.getBooleanExtra("boot", true);

        setUpGameData();

        if (preLaunch) {
            reportHost.setVisibility(View.GONE);
            optionsPanel.openPreLaunch(launchState());
            // PLAN 13.1: the launcher only - a start straight into the game never asks.
            appUpdate = new AppUpdate(this,
                    () -> optionsPanel.setLaunchState(launchState()));
            appUpdate.check();
        }
        bootWhenReady();
    }

    /**
     * Starts the game once its data is here and the surface exists. Called at launch,
     * and again when an extraction ends, so a first launch plays once the disc is
     * extracted. The report is hidden meanwhile - the title's first frames come a few
     * seconds later - and comes back with the reason if the game cannot start; while
     * the data is being extracted or asked for, the report is what shows that.
     */
    private void bootWhenReady() {
        if (!bootWanted || guestBooted || bootPending) {
            return;
        }
        String gameData = GameData.destination(this).getAbsolutePath();
        if (!NativeBridge.extractIsComplete(gameData)) {
            return;
        }
        // Now, not at startup: the button prompts, the intro logos and the title update
        // as the settings hold them as the game launches. Once per process.
        NativeBridge.loadTitle(gameData);
        bootPending = true;
        reportHost.setVisibility(View.GONE);
        handler.post(new Runnable() {
            private int waitedMs;

            @Override
            public void run() {
                // After the surface, so the title's presenter goes straight onto the
                // window - the order every run until now had. Not forever: a surface
                // that never comes must not keep the game from starting.
                if (!surfaceView.getHolder().getSurface().isValid() && waitedMs < 3000) {
                    waitedMs += 50;
                    handler.postDelayed(this, 50);
                    return;
                }
                bootPending = false;
                boolean started = NativeBridge.bootGuest();
                Log.i(TAG, "bootGuest() = " + started + " (surface waited " + waitedMs
                        + " ms)");
                if (started) {
                    guestBooted = true;
                    LastExit.mark(GameActivity.this, "playing");
                    // PLAN 8: the companion deck on the second screen, as the game runs.
                    boolean deckShowing = deck.open(GameActivity.this, getFilesDir());
                    // The performance numbers: the deck's Perf tab when it shows, else
                    // the overlay here - either only while the option is on.
                    perfOverlay.setDeckShowing(deckShowing);
                    perfOverlay.start();
                    touchControls.start();
                    photoOverlay.start();
                    watchBatterySaver();
                    // The report stays in logcat; what the surface shows is the game.
                    pollKeyboard();
                } else {
                    reportHost.setVisibility(View.VISIBLE);
                    appendReport("GAME NOT STARTED\n" + NativeBridge.bootReport());
                }
            }
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        inFront = true;
        if (appUpdate != null) {
            appUpdate.resumed();
        }
        reportMediaVolume();
        goFullscreen();
        if (runtimeStarted) {
            NativeBridge.setPaused(false);
        }
        if (guestBooted) {
            LastExit.mark(this, "playing");
        }
    }

    /**
     * The deck follows the game (the owner, 2026-11-02): put away when the game goes to
     * the background - one Home press then shows home on both screens - and back when the
     * game is.
     */
    @Override
    protected void onStart() {
        super.onStart();
        if (guestBooted) {
            deck.show(this, getFilesDir());
        }
    }

    @Override
    protected void onStop() {
        if (guestBooted && !isFinishing()) {
            deck.hide(this);
        }
        super.onStop();
    }

    @Override
    protected void onPause() {
        inFront = false;
        if (runtimeStarted) {
            NativeBridge.setPaused(true);
        }
        if (guestBooted) {
            LastExit.mark(this, "held");
        }
        super.onPause();
    }

    /**
     * Pad buttons arrive here, not at the SurfaceView: a {@link SurfaceView} is not
     * focusable, so the activity is what has focus in a layout like this one.
     *
     * <p>Unhandled codes fall through to {@code super}. That matters on a handheld,
     * where the volume keys and the power button arrive as key events too - swallowing
     * everything would make them dead while the game is running.
     */
    /**
     * PLAN 10.1: the options panel, open, takes the controller's keys before anything
     * else - releases of keys pressed before it opened still reach the game - and Select
     * with Start opens it.
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        // A pad that reports its face buttons by label (the Thor's own) as the
        // bottom-is-A convention has them, for everything below (ControllerManager).
        if (controllers != null) {
            event = controllers.positional(event);
        }
        if (optionsPanel == null) {
            return super.dispatchKeyEvent(event);
        }
        // PLAN 10.21: placing the on-screen controls, the controller's A, B or Start is Done.
        if (touchControls != null && touchControls.isEditing()) {
            return touchControls.onEditKey(event) || super.dispatchKeyEvent(event);
        }
        if (optionsCombo.swallows(event)) {
            return true;
        }
        boolean wasOpen = optionsPanel.isOpen();
        if (optionsPanel.onKey(event)) {
            if (wasOpen && !optionsPanel.isOpen() && isMenuKey(event.getKeyCode())) {
                optionsCombo.closedByMenuKey();
            }
            return true;
        }
        if (wasOpen) {
            if (event.getAction() == KeyEvent.ACTION_UP && controllers != null
                    && controllers.onKey(event)) {
                return true;
            }
            return super.dispatchKeyEvent(event);
        }
        if (guestBooted && !ending && optionsCombo.onKey(event)) {
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (controllers != null && controllers.onKey(event)) {
            refreshPadReport();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (controllers != null && controllers.onKey(event)) {
            refreshPadReport();
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (touchControls != null && touchControls.isEditing()) {
            return true;  // PLAN 10.21: the sticks do nothing while the controls are placed
        }
        if (optionsPanel != null && optionsPanel.onMotion(event)) {
            return true;
        }
        if (controllers != null && controllers.onMotion(event)) {
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    /**
     * Scaffolding for this phase only: shows live pad state on the handheld's own
     * screen so the mapping can be checked by pressing buttons, without a second
     * machine running logcat. Goes away with the rest of the overlay once there is a
     * game to look at.
     */
    private void refreshPadReport() {
        if (report == null) {
            return;
        }
        report.setText(padReportHeader + NativeBridge.padReport());
    }

    /** PLAN 10.2: Android's battery saver, for the heat guard (which alone acts on it). */
    private BroadcastReceiver batterySaverReceiver;

    /** PLAN 10.7: how the last session ended (LastExit), "" when as asked. */
    private String lastExit = "";

    private void watchBatterySaver() {
        if (batterySaverReceiver != null) {
            return;
        }
        final PowerManager power = getSystemService(PowerManager.class);
        batterySaverReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                NativeBridge.applyOption("power-save", power.isPowerSaveMode() ? "on" : "off");
            }
        };
        registerReceiver(batterySaverReceiver,
                new IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED),
                Context.RECEIVER_NOT_EXPORTED);
        NativeBridge.applyOption("power-save", power.isPowerSaveMode() ? "on" : "off");
    }

    @Override
    protected void onDestroy() {
        if (batterySaverReceiver != null) {
            unregisterReceiver(batterySaverReceiver);
            batterySaverReceiver = null;
        }
        // Stop the extraction before the process goes: it holds a descriptor and is
        // writing into a directory, and letting it run past teardown would leave a
        // .part file behind for the resume logic to trip over.
        NativeBridge.extractCancel();
        handler.removeCallbacksAndMessages(null);
        if (controllers != null) {
            controllers.stop();
            controllers = null;
        }
        deck.close();
        if (perfOverlay != null) {
            perfOverlay.stop();
        }
        if (touchControls != null) {
            touchControls.stop();
        }
        if (photoOverlay != null) {
            photoOverlay.stop();
        }
        if (audioManager != null && audioDevices != null) {
            audioManager.unregisterAudioDeviceCallback(audioDevices);
            audioDevices = null;
        }
        if (mediaVolume != null) {
            unregisterReceiver(mediaVolume);
            mediaVolume = null;
        }
        if (guestBooted) {
            // The title's threads are still running in guest memory that shutdown()
            // would free under them, so the process ends here instead; the system
            // reclaims the GPU's and the audio stream's resources with it (task 4.9).
            Log.i(TAG, "onDestroy: the guest has run; ending the process");
            LastExit.mark(this, "ending");  // PLAN 10.7: the app's own end, not a crash
            android.os.Process.killProcess(android.os.Process.myPid());
        }
        // The surface is released by GameSurfaceView's own callback, which Android
        // delivers before this. shutdown() tolerates having never started.
        NativeBridge.shutdown();
        super.onDestroy();
    }

    /**
     * Back while the game runs asks first: quitting ends the process, and whatever the
     * game has not saved is gone. Before the guest starts (extraction, the report) Back
     * closes the app as usual.
     */
    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (!guestBooted) {
            super.onBackPressed();
            return;
        }
        if (ending) {
            return;
        }
        new android.app.AlertDialog.Builder(this)
                .setTitle("Quit Dragon Age: Origins?")
                .setMessage("Progress since the last save will be lost.")
                .setPositiveButton("Quit", (dialog, which) -> {
                    NativeBridge.requestQuit("the player quit (Back)");
                    endProcess();
                })
                .setNegativeButton(android.R.string.cancel, null)
                .setNeutralButton("Settings", (dialog, which) -> openOptions())
                .setOnDismissListener(dialog -> goFullscreen())
                .show();
    }

    // ---- the audio route (PLAN 9.12) ----

    private AudioManager audioManager;
    private AudioDeviceCallback audioDevices;

    /**
     * Hands the native side the ids of the output devices that are headphones - wired,
     * USB or Bluetooth. While the game's sound plays on one of them, Settings ->
     * Headphones decides its mix (warden_audio.cc). A Bluetooth speaker cannot be told
     * from headphones here; Android calls both A2DP.
     */
    private void reportHeadphones() {
        AudioDeviceInfo[] outputs = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
        int[] ids = new int[outputs.length];
        int count = 0;
        StringBuilder names = new StringBuilder();
        for (AudioDeviceInfo device : outputs) {
            switch (device.getType()) {
                case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
                case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                case AudioDeviceInfo.TYPE_USB_HEADSET:
                case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
                case AudioDeviceInfo.TYPE_BLE_HEADSET:
                    ids[count++] = device.getId();
                    names.append(' ').append(device.getId()).append(" (")
                            .append(device.getProductName()).append(')');
                    break;
                default:
                    break;
            }
        }
        Log.i(TAG, "audio: headphones:" + (count == 0 ? " none" : names.toString()));
        NativeBridge.audioHeadphones(java.util.Arrays.copyOf(ids, count));
    }

    /**
     * BUG-109: on the AYN Thor the media volume does not reach this track: the mixer's stream
     * volume moves and the track's final gain does not, for usage game and media alike. So
     * the sound takes the volume here, on those devices only - anywhere Android applies it,
     * it would be applied twice. Run 613: from index 15 to 8 the output drops 13 dB, this
     * gain alone; 26 would be both.
     *
     * <p>BUG-116 found why: AYN gives an app with an activity on the second screen that
     * screen's own volume (sys.audio.uids, secondary_screen_volume_level) instead of the media
     * volume - and DeckActivity put the game there. The deck is a Presentation now (Route B),
     * the game is never listed, and Android applies the media volume itself; doing it here as
     * well made it twice (run 690: 27 dB from 15 to 8 with no deck). So only with Route A, the
     * test switch.
     */
    private boolean followsMediaVolumeItself() {
        return "AYN".equalsIgnoreCase(android.os.Build.MANUFACTURER)
                && Deck.usesActivity(getFilesDir());
    }

    private BroadcastReceiver mediaVolume;

    /**
     * The media volume to the native side, by Android's own curve for the output in use. Not
     * over Bluetooth: there the headset takes the volume itself.
     */
    private void reportMediaVolume() {
        if (audioManager == null || !followsMediaVolumeItself()) {
            return;
        }
        int deviceType = AudioDeviceInfo.TYPE_BUILTIN_SPEAKER;
        for (AudioDeviceInfo device : audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
            switch (device.getType()) {
                case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
                case AudioDeviceInfo.TYPE_BLE_HEADSET:
                    NativeBridge.audioMediaVolume(1f);
                    return;
                case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
                case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                case AudioDeviceInfo.TYPE_USB_HEADSET:
                    deviceType = device.getType();
                    break;
                default:
                    break;
            }
        }
        int index = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC);
        float gain = 0f;
        if (index > 0 && !audioManager.isStreamMute(AudioManager.STREAM_MUSIC)) {
            float db = audioManager.getStreamVolumeDb(AudioManager.STREAM_MUSIC, index, deviceType);
            gain = (float) Math.pow(10.0, db / 20.0);
        }
        NativeBridge.audioMediaVolume(gain);
    }

    // ---- the options panel (PLAN 10.1) ----

    private OptionsPanel optionsPanel;
    private final OptionsCombo optionsCombo = new OptionsCombo();

    private final OptionsPanel.Host optionsHost = new OptionsPanel.Host() {
        @Override
        public void onOptionsClosed() {
            // The sticks, triggers and hat as they are now: moved while the panel was
            // open, the game has not seen them.
            if (controllers != null) {
                optionsPanel.takeMotions(controllers::onMotion);
            }
            NativeBridge.holdGame(false);
            goFullscreen();
        }

        @Override
        public void onAdjustTouchLayout() {
            // PLAN 10.21: the panel out of the way (the game still held), the controls to
            // drag; their Done brings the panel back.
            optionsPanel.hideForEdit();
            touchControls.startEditing(() -> optionsPanel.showAfterEdit());
        }

        @Override
        public void onPhotoMode() {
            // The panel closes (the game runs again), then photo mode at its next frame.
            optionsPanel.close();
            NativeBridge.photoToggle();
        }

        @Override
        public void onQuitChosen() {
            if (!guestBooted) {
                // Before the game: nothing to lose.
                endProcess();
                return;
            }
            NativeBridge.requestQuit("the player quit (options)");
            endProcess();
        }

        @Override
        public void onBuildChosen() {
            startBuild();
        }

        @Override
        public void onAppUpdateChosen() {
            if (appUpdate == null) {
                return;
            }
            // The owner (2026-11-03): the release notes first, then the install - or, for
            // a manual update, the release's page.
            int state = appUpdate.state();
            if (state == AppUpdate.AVAILABLE || state == AppUpdate.MANUAL) {
                showReleaseNotes(state == AppUpdate.AVAILABLE);
            } else {
                appUpdate.choose();
            }
        }

        @Override
        public void onLaunchChosen() {
            if (!preLaunch) {
                return;
            }
            preLaunch = false;
            if (appUpdate != null) {
                appUpdate.cancel();
            }
            bootWanted = true;
            optionsPanel.beginGame();
            Log.i(TAG, "launch: Launch Dragon Age chosen in the settings");
            bootWhenReady();
        }

        @Override
        public void onReleasesPage() {
            // PLAN 13.2: the browser opens the page; the app fetches nothing for it.
            String page = BuildConfig.SOURCE_URL + "/releases";
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(page)));
            } catch (RuntimeException e) {
                Log.w(TAG, "options: no browser for " + page, e);
            }
        }

        @Override
        public void onMods() {
            // PLAN 10.12: before the game only - the mods are laid over the disc as it starts.
            if (preLaunch) {
                modsPage = new ModsPage(GameActivity.this, () -> goFullscreen());
                modsPage.show();
            }
        }

        @Override
        public void onRestoreSaves() {
            // PLAN 10.15, 11.2: before the game only - its saves are not in use. The Saves
            // page: export, import, and the restore of the saves the game wrote over.
            if (preLaunch) {
                savesPage = new SavesPage(GameActivity.this, getFilesDir(), () -> goFullscreen());
                savesPage.show();
            }
        }

        @Override
        public void onAbout() {
            // PLAN 11.5: before the game or during it (the game stays held meanwhile).
            AboutPage.show(GameActivity.this, () -> goFullscreen());
        }

        @Override
        public boolean debugMode() {
            return debugMode;
        }

        @Override
        public void onExportReport() {
            if (!guestBooted || exporting) {
                return;
            }
            exportReport();
        }

        @Override
        public void onPickFile(int what) {
            wantImage = what == OptionsPanel.PICK_IMAGE;
            wantUpdate = what == OptionsPanel.PICK_UPDATE;
            wantLossless = what == OptionsPanel.PICK_LOSSLESS;
            wantUltimate = what == OptionsPanel.PICK_ULTIMATE;
            try {
                startActivityForResult(GameData.pickFilesIntent(), REQUEST_GAME_FILES);
            } catch (Exception e) {
                Log.e(TAG, "no document picker available", e);
                pickNote = "This device has no file picker.";
                optionsPanel.setLaunchState(launchState());
            }
        }
    };

    /**
     * The pre-launch rows as things are: the game data extracted (or how far the
     * extraction is), the title update in place, and whether Launch is offered.
     */
    /** PLAN 13.1: the launcher's update check, none in a start straight into the game. */
    private AppUpdate appUpdate;

    private OptionsPanel.LaunchState launchState() {
        OptionsPanel.LaunchState state = new OptionsPanel.LaunchState();
        if (appUpdate != null && appUpdate.state() != AppUpdate.NOTHING) {
            state.appUpdateName = appUpdate.rowName();
            state.appUpdateValue = appUpdate.rowValue();
            state.appUpdateDetail = appUpdate.rowDetail();
        }
        long[] progress = NativeBridge.extractProgress();
        long extractState = progress != null && progress.length > 0 ? progress[0] : 0;
        boolean extracting = extractState == 1 || extractState == 2;
        boolean extracted = !extracting
                && NativeBridge.extractIsComplete(GameData.destination(this).getAbsolutePath());
        String note = pickNote.isEmpty() ? "" : pickNote + " ";

        if (extracting) {
            long done = progress.length > 2 ? progress[1] : 0;
            long total = progress.length > 2 ? progress[2] : 0;
            int percent = total > 0 ? (int) (done * 100 / total) : 0;
            state.imageValue = extractState == 1 ? "Reading…" : "Extracting " + percent + "%";
            state.imageDetail = note + String.format(java.util.Locale.ROOT,
                    "Extracting the disc image into the app's storage: %.1f of %.1f GB."
                            + " The game can launch when it is done.",
                    done / 1e9, total / 1e9);
        } else if (extracted) {
            state.imageValue = "Extracted";
            state.imageDetail = note + "The game's files are extracted from the disc image"
                    + " into the app's storage. Choosing the image again checks them: files"
                    + " already whole are kept.";
        } else if (extractState == 4) {
            state.imageValue = "Not finished";
            state.imageDetail = note + "The extraction stopped before the end. Choose the"
                    + " disc image again to carry on where it stopped.";
        } else {
            state.imageValue = "Choose…";
            state.imageDetail = note + "Your Dragon Age: Origins disc image (.iso). Its files"
                    + " are extracted once into the app's storage - about 6.4 GB, a few"
                    + " minutes.";
        }

        // Lossless Scaling's frame generation reads the owner's Lossless.dll from the
        // files folder (warden_frame_generation.cc); the row puts it there.
        if (GameData.hasLossless(this)) {
            state.losslessValue = "In place";
            state.losslessDetail = note + "Lossless Scaling's frame generation (Frame"
                    + " generation -> Lossless 2x) reads it. Choose another to replace it.";
        } else {
            state.losslessValue = "Choose…";
            state.losslessDetail = note + "Lossless.dll from your own copy of Lossless"
                    + " Scaling (its install folder), for Frame generation -> Lossless 2x."
                    + " It stays on this device.";
        }

        // PLAN 12.3 (the owner, 2026-11-02): Title Update 7 optional, recommended - disc 2
        // carries Title Update 6, which the disc 2 row puts in place.
        if (titleUpdate == null) {
            state.updateValue = "Choose…";
            state.updateDetail = note + "Optional, recommended: Title Update 7 (the TU_..."
                    + " file from your console's storage). Without it, the Ultimate Edition's"
                    + " disc 2 gives the game Title Update 6 - one of the two is needed.";
        } else if (titleUpdate.number == 7) {
            state.updateValue = "In place";
            state.updateDetail = note + "Title Update 7 in the app's storage ("
                    + titleUpdate.file + "): the game is made from it.";
        } else if (titleUpdate.number == 6) {
            state.updateValue = titleUpdate.fromDisc2(this) ? "disc 2's TU6 instead" : "TU6 instead";
            state.updateDetail = note + "Title Update 6 is in place"
                    + (titleUpdate.fromDisc2(this) ? ", from disc 2" : "") + ". Optional,"
                    + " recommended: choose the Title Update 7 file (TU_...) to use it instead.";
        } else {
            state.updateValue = "Not usable";
            state.updateDetail = note + titleUpdate.file + " is a title update the game cannot"
                    + " use (not Title Update 6 or 7). Choose Title Update 7, or disc 2 for 6.";
        }

        // PLAN 10.10: Awakening and the DLC, from the Ultimate Edition's disc 2.
        int packs = GameData.dlcCount(this);
        if (ultimateInstalling) {
            long[] dlc = NativeBridge.dlcProgress();
            long done = dlc != null && dlc.length > 1 ? dlc[0] : 0;
            long total = dlc != null && dlc.length > 1 ? dlc[1] : 0;
            state.ultimateValue = "Installing " + (total > 0 ? done * 100 / total : 0) + "%";
            state.ultimateDetail = String.format(java.util.Locale.ROOT,
                    "Installing Awakening and the DLC from disc 2: %.1f of %.1f GB.",
                    done / 1e9, total / 1e9);
        } else if (packs > 0) {
            state.ultimateValue = packs + " installed";
            state.ultimateDetail = note + "Awakening and the DLC from the Ultimate Edition's"
                    + " disc 2: New Game offers the campaigns, Downloadable Content lists them.";
        } else {
            state.ultimateValue = "Choose…";
            state.ultimateDetail = note + "The Ultimate Edition's disc 2 (Additional Content"
                    + " Packs Install Disc, .iso): its Title Update 6, which the game needs"
                    + " unless you choose Title Update 7, and Awakening, Leliana's Song, Witch"
                    + " Hunt and every other DLC on it, installed in the app's storage (4.6 GB).";
        }

        boolean usableUpdate = titleUpdate != null && titleUpdate.number >= 6;
        // PLAN 12.5, 12.8: the game's code, made on this device from the disc and the
        // title update - when the player chooses Build (the owner, 2026-11-02: not before
        // they are ready, so Title Update 7 can be added first), and again by Build when
        // the app or the title update changes.
        boolean codeReady = usableUpdate && GameCode.current(this, titleUpdate.number);
        boolean codeRunning = GameCode.running(this);
        String[] making = GameCode.state(this);
        boolean codeFailed = making != null && GameCode.Progress.FAILED.equals(making[0]);
        // Just started: the service's first heartbeat can take a moment, but a failure
        // written since the start is a failure.
        boolean failedSinceStart = codeFailed && GameCode.stateTime(this) >= gameCodeStarted;
        boolean codeStarting = !codeRunning && !failedSinceStart
                && System.currentTimeMillis() - gameCodeStarted < 60_000;
        boolean madeBefore = GameCode.library(this).isFile();
        if (codeRunning) {
            pollGameCode();  // a making started earlier, the app opened again meanwhile
        }
        boolean filesBusy = extracting || ultimateInstalling;
        state.canBuild = extracted && usableUpdate && !filesBusy && !codeReady
                && !codeRunning && !codeStarting;
        String fromUpdate = usableUpdate ? "Title Update " + titleUpdate.number
                + (titleUpdate.number == 6 && titleUpdate.fromDisc2(this) ? " (from disc 2)" : "")
                : "";
        if (codeRunning || codeStarting) {
            long done = making != null && codeRunning ? Long.parseLong(making[1]) : 0;
            long total = making != null && codeRunning ? Long.parseLong(making[2]) : 1;
            state.buildValue = "building " + (total > 0 ? done * 100 / total : 0) + "%";
            state.buildDetail = "Making the game's code from your disc and " + fromUpdate
                    + ", for this device - once, about eight minutes on the Thor"
                    + (making != null && codeRunning ? ": " + making[3].trim() : "")
                    + ". You can put the app away meanwhile; keep the device charging.";
        } else if (codeReady) {
            state.buildValue = "Done";
            state.buildDetail = "The game's code is made, from " + fromUpdate + ". Choosing"
                    + " another title update needs Build again.";
        } else if (!extracted || !usableUpdate) {
            state.buildValue = "not yet";
            state.buildDetail = "Makes the game's code from your files, once. First "
                    + (!extracted && !usableUpdate
                            ? "the disc image and disc 2 (or Title Update 7)"
                            : !extracted ? "the disc image" : "disc 2 (or Title Update 7)")
                    + "; add Title Update 7 too if you have it, before building.";
        } else if (filesBusy) {
            state.buildValue = "not yet";
            state.buildDetail = "Once " + (extracting ? "the disc image is extracted"
                    : "disc 2 is installed") + ". Add Title Update 7 too if you have it,"
                    + " before building.";
        } else if (codeFailed) {
            state.buildValue = "failed";
            state.buildDetail = (making != null ? making[3].trim()
                    : "The game's code could not be made.") + " Choose Build to try again.";
        } else {
            state.buildValue = madeBefore ? "Build again" : "Start";
            state.buildDetail = (madeBefore ? "The game's code was made for another title"
                    + " update or app version. " : "")
                    + "Makes the game's code from your disc and " + fromUpdate + ", for this"
                    + " device - once, about eight minutes on the Thor"
                    + (titleUpdate.number == 6 ? ". Have Title Update 7? Choose it first"
                            + " (recommended): the code is made from the title update in place"
                            : "")
                    + ". Keep the device charging.";
        }
        state.ready = extracted && usableUpdate && codeReady;
        if (state.ready) {
            state.launchDetail = "Starts Dragon Age: Origins with the options below.";
            state.status = "Ready";
        } else if (codeRunning || codeStarting) {
            state.launchDetail = "Once the game's code is made (Build, below).";
            state.status = "Building the game's code";
        } else if (extracting) {
            state.launchDetail = "Once the disc image is extracted"
                    + (!usableUpdate ? " and disc 2 (or Title Update 7) chosen" : "")
                    + ", and the game's code built.";
            state.status = state.imageValue;
        } else if (extracted && usableUpdate) {
            state.launchDetail = "Once the game's code is made: choose Build, below, when"
                    + " your files are in.";
            state.status = codeFailed ? "Building failed" : "Ready to build";
        } else {
            state.launchDetail = "Choose " + (!extracted && !usableUpdate
                    ? "the disc image and disc 2 (or Title Update 7)"
                    : !extracted ? "the disc image" : "disc 2 (or Title Update 7)")
                    + " first, then Build.";
            state.status = "Setting up";
        }
        String tests = testFiles();
        if (!tests.isEmpty()) {
            state.launchDetail += " Test files in place: " + tests + " - a computer's test"
                    + " run left them, and they change the game (bash tools/route.sh off).";
            state.status = "Test files in place";
        }
        // PLAN 10.7: how the last session ended, when it was not as asked.
        if (!lastExit.isEmpty()) {
            state.launchDetail += " " + lastExit;
        }
        return state;
    }

    /**
     * The owner, 2026-11-01: the options panel by touch. Three fingers on the game's
     * screen open it - unless the on-screen controller is on, whose own button opens it
     * (and where three fingers are play).
     */
    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_POINTER_DOWN
                && event.getPointerCount() == 3 && guestBooted && !ending
                && optionsPanel != null && !optionsPanel.isOpen()
                && !TouchControls.enabled(getFilesDir())) {
            Log.i(TAG, "options: three fingers on the screen");
            openOptions();
            return true;
        }
        return super.dispatchTouchEvent(event);
    }

    /** Opens the options panel over the game, which holds while it is open. */
    private void openOptions() {
        if (optionsPanel == null || optionsPanel.isOpen() || !guestBooted || ending) {
            return;
        }
        NativeBridge.holdGame(true);
        optionsPanel.open();
    }

    private static boolean isMenuKey(int keyCode) {
        return keyCode == KeyEvent.KEYCODE_BUTTON_START
                || keyCode == KeyEvent.KEYCODE_BUTTON_SELECT;
    }

    /**
     * Select and Start pressed together open the options panel (PLAN 10.1).
     *
     * <p>Each is held back from the game for a moment - {@link #WINDOW_MS}, or until it
     * is let go - so a press of both reaches neither the game's map nor its menu; alone,
     * it reaches the game that much later. Pressed one after the other, the first has
     * reached the game by then: it is let go there, and the panel opens all the same.
     */
    private final class OptionsCombo {
        static final long WINDOW_MS = 150;
        /** A tap let go inside the window still reaches the game, held this long. */
        static final long TAP_MS = 80;

        /** The key held back, 0 for none, and its device. */
        private int heldKey;
        private int heldDevice;
        /** Start and Select passed to the game and still down: key code to device. */
        private final android.util.SparseIntArray down = new android.util.SparseIntArray();
        /** Keys whose release is nobody's: they opened or closed the panel. */
        private final java.util.Set<Long> swallowed = new java.util.HashSet<>();
        /** When the panel was closed by Start or Select: the other, pressed with it, is swallowed too. */
        private long closedAt = -1;

        private final Runnable deliver = new Runnable() {
            @Override
            public void run() {
                if (heldKey == 0) {
                    return;
                }
                padKey(heldDevice, heldKey, true);
                down.put(heldKey, heldDevice);
                heldKey = 0;
            }
        };

        private long key(int device, int keyCode) {
            return ((long) device << 32) | (keyCode & 0xFFFFFFFFL);
        }

        private void padKey(int device, int keyCode, boolean pressed) {
            int slot = controllers == null ? -1 : controllers.slotOf(device);
            if (slot >= 0) {
                NativeBridge.padKey(slot, keyCode, pressed);
            }
        }

        void closedByMenuKey() {
            closedAt = android.os.SystemClock.uptimeMillis();
        }

        /** The releases of the keys that opened or closed the panel, and a second key closing it. */
        boolean swallows(KeyEvent event) {
            long key = key(event.getDeviceId(), event.getKeyCode());
            if (event.getAction() == KeyEvent.ACTION_UP) {
                return swallowed.remove(key);
            }
            if (event.getAction() == KeyEvent.ACTION_DOWN && isMenuKey(event.getKeyCode())
                    && closedAt >= 0
                    && android.os.SystemClock.uptimeMillis() - closedAt < 2 * WINDOW_MS) {
                swallowed.add(key);
                return true;
            }
            return false;
        }

        boolean onKey(KeyEvent event) {
            int code = event.getKeyCode();
            if (!isMenuKey(code)) {
                return false;
            }
            int device = event.getDeviceId();
            int other = code == KeyEvent.KEYCODE_BUTTON_START
                    ? KeyEvent.KEYCODE_BUTTON_SELECT : KeyEvent.KEYCODE_BUTTON_START;
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (event.getRepeatCount() > 0) {
                    return true;
                }
                if (heldKey == other && heldDevice == device) {
                    // Both together: neither reaches the game.
                    handler.removeCallbacks(deliver);
                    heldKey = 0;
                    swallowed.add(key(device, other));
                    swallowed.add(key(device, code));
                    Log.i(TAG, "options: Select + Start");
                    openOptions();
                    return true;
                }
                if (down.get(other, Integer.MIN_VALUE) == device) {
                    // The other has reached the game: it is let go there.
                    down.delete(other);
                    padKey(device, other, false);
                    swallowed.add(key(device, other));
                    swallowed.add(key(device, code));
                    Log.i(TAG, "options: Select + Start, one after the other");
                    openOptions();
                    return true;
                }
                if (heldKey != 0) {
                    handler.removeCallbacks(deliver);
                    deliver.run();
                }
                heldKey = code;
                heldDevice = device;
                handler.postDelayed(deliver, WINDOW_MS);
                return true;
            }
            if (event.getAction() == KeyEvent.ACTION_UP) {
                if (heldKey == code && heldDevice == device) {
                    // A tap inside the window: the press now, the release a little later,
                    // so the game's next frames see it.
                    handler.removeCallbacks(deliver);
                    heldKey = 0;
                    padKey(device, code, true);
                    handler.postDelayed(() -> padKey(device, code, false), TAP_MS);
                    return true;
                }
                if (down.get(code, Integer.MIN_VALUE) == device) {
                    down.delete(code);
                    padKey(device, code, false);
                    return true;
                }
            }
            return false;
        }
    }

    /**
     * The native side asked the app to end ({@link NativeBridge#exitRequest}): at once
     * when the title ended itself, after a message when something failed.
     */
    private void handleExit(String exit) {
        if (ending) {
            return;
        }
        Log.i(TAG, "exit requested: " + exit);
        if (exit.startsWith("error:")) {
            ending = true;
            new android.app.AlertDialog.Builder(this)
                    .setTitle("Dragon Age: Origins has to close")
                    .setMessage(exit.substring("error:".length()))
                    .setCancelable(false)
                    .setPositiveButton(android.R.string.ok, (dialog, which) -> endProcess())
                    .show();
            return;
        }
        endProcess();
    }

    /** Closes the task and ends the process (onDestroy, once the guest has run). */
    private void endProcess() {
        ending = true;
        finishAndRemoveTask();
    }

    /** PLAN 11.1: a report under way (one at a time). */
    private boolean exporting;

    /**
     * PLAN 11.1, "Exit and export save/logs": the panel closes and the game runs on for its
     * own autosave (where it allows one), the screen is copied, the game held, the report
     * packed into Downloads off the UI thread (DebugReport.export), its name shown - and
     * the game ends.
     */
    private void exportReport() {
        exporting = true;
        optionsPanel.close();
        android.app.AlertDialog working = new android.app.AlertDialog.Builder(this,
                android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setMessage("Making the bug report: a save first, then the logs...")
                .setCancelable(false).show();
        Log.i(TAG, "debug mode: exporting a report");
        new Thread(() -> {
            long asked = System.currentTimeMillis();
            int saved = NativeBridge.saveForReport();
            handler.post(() -> {
                int width = surfaceView.getWidth();
                int height = surfaceView.getHeight();
                if (width <= 0 || height <= 0) {
                    packReport(working, null, saved, asked);
                    return;
                }
                android.graphics.Bitmap shot = android.graphics.Bitmap.createBitmap(width, height,
                        android.graphics.Bitmap.Config.ARGB_8888);
                android.view.PixelCopy.request(surfaceView, shot, result ->
                        packReport(working, result == android.view.PixelCopy.SUCCESS ? shot : null,
                                saved, asked), handler);
            });
        }, "debug-report").start();
    }

    private void packReport(android.app.AlertDialog working, android.graphics.Bitmap shot,
                            int saved, long asked) {
        NativeBridge.holdGame(true);
        new Thread(() -> {
            String name = DebugReport.export(this, getFilesDir(), shot, saved, asked);
            handler.post(() -> {
                working.dismiss();
                String text = name != null
                        ? "The report is in Downloads/Dragon Age Origins:\n\n" + name
                                + "\n\nSend it with a description of what happened - from a"
                                + " file manager or a messenger. The game ends now."
                                + (saved == 1 ? "" : saved == 0
                                        ? "\n\n(No fresh save: the game allows none here.)"
                                        : "\n\n(The fresh save did not finish in time.)")
                        : "The report could not be written (is the storage full?). The game"
                                + " ends now.";
                new android.app.AlertDialog.Builder(this,
                        android.R.style.Theme_DeviceDefault_Dialog_Alert)
                        .setMessage(text)
                        .setCancelable(false)
                        .setPositiveButton("Quit", (d, w) -> {
                            NativeBridge.requestQuit("the player exported a bug report");
                            endProcess();
                        })
                        .show();
            });
        }, "debug-report-pack").start();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            // The system bars come back on their own after a swipe; put them away again.
            goFullscreen();
        }
    }

    /** Request code for the file picker; the only one this activity uses. */
    private static final int REQUEST_GAME_FILES = 1;
    /** PLAN 10.12: the Mods page, while it picks a mod. */
    private ModsPage modsPage;
    /** PLAN 11.2: the Saves page while open (its import's file pick comes back here). */
    private SavesPage savesPage;
    /** What the open picker was asked for, so its result is judged against that. */
    private boolean wantImage;
    private boolean wantUpdate;
    /** The pre-launch row asked for Lossless.dll. */
    private boolean wantLossless;
    /** PLAN 10.10: the pre-launch row asked for the Ultimate Edition's disc 2, and its install. */
    private boolean wantUltimate;
    private boolean ultimateInstalling;
    /**
     * The title was loaded at startup without a title update. {@code LoadTitle} runs
     * once per process, so an update picked now takes effect on the next launch.
     */
    private boolean loadedWithoutUpdate;

    /**
     * Decides what to do about game data, in the order that asks least of the user
     * (tasks 4.3 and 4.4).
     *
     * <ol>
     *   <li>Already extracted - nothing to do for the image.</li>
     *   <li>An image in app storage, or one picked earlier whose grant is still held -
     *       start extracting, or resume.</li>
     *   <li>Otherwise ask for it. The title update is asked for in the same trip to the
     *       picker if app storage does not already have one.</li>
     * </ol>
     */
    private void setUpGameData() {
        java.io.File target = GameData.destination(this);
        boolean extracted = NativeBridge.extractIsComplete(target.getAbsolutePath());
        boolean needImage = false;
        if (extracted) {
            appendReport("GAME DATA\n  already extracted to\n    "
                    + target.getAbsolutePath() + "\n");
        } else {
            // Before anything that needs a grant: an image the app can already open.
            java.io.File direct = GameData.directImage(this);
            Uri remembered = direct == null ? GameData.rememberedImage(this) : null;
            if (direct != null) {
                appendReport("GAME DATA\n  found " + direct.getName() + "  "
                        + direct.length() + " bytes\n  (app storage - no grant needed)\n");
                if (GameData.startExtraction(this, direct)) {
                    pollExtraction();
                }
            } else if (remembered != null) {
                appendReport("GAME DATA\n  resuming from the disc image picked earlier\n");
                if (GameData.startExtraction(this, remembered)) {
                    pollExtraction();
                }
            } else {
                needImage = true;
            }
        }
        titleUpdate = GameData.titleUpdate(this);
        boolean needUpdate = titleUpdate == null;
        // The title loads as the game launches; for the report alone (`--ez boot false`)
        // at once, so the report says how the load went.
        boolean reportOnly = !preLaunch && !bootWanted;
        if (extracted && reportOnly) {
            NativeBridge.loadTitle(target.getAbsolutePath());
        }
        loadedWithoutUpdate = extracted && needUpdate && reportOnly;
        // Before the game the settings' rows ask, when chosen.
        if ((needImage || needUpdate) && !preLaunch) {
            askForFiles(needImage, needUpdate);
        }
    }

    private void askForFiles(boolean image, boolean update) {
        wantImage = image;
        wantUpdate = update;
        String what = image && update
                ? "the disc image (.iso) and the title update (TU_...)"
                : image ? "the disc image (.iso)"
                : "the title update (TU_..., Title Update 7)";
        appendReport("GAME FILES\n  asking for " + what + "\n"
                + "  long-press to select more than one file\n");
        try {
            startActivityForResult(GameData.pickFilesIntent(), REQUEST_GAME_FILES);
        } catch (Exception e) {
            // A device with no documents provider at all. Unlikely, but failing here
            // must not take the renderer down with it.
            Log.e(TAG, "no document picker available", e);
            appendReport("  no document picker on this device\n");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == ModsPage.REQUEST_MOD_ZIP || requestCode == ModsPage.REQUEST_MOD_FOLDER) {
            if (modsPage != null) {
                modsPage.onPicked(resultCode == RESULT_OK && data != null ? data.getData() : null,
                        requestCode == ModsPage.REQUEST_MOD_FOLDER);
            }
            return;
        }
        if (requestCode == SavesPage.REQUEST_SAVE_ZIP) {
            if (savesPage != null) {
                savesPage.onPicked(resultCode == RESULT_OK && data != null ? data.getData() : null);
            }
            return;
        }
        if (requestCode != REQUEST_GAME_FILES) {
            return;
        }
        if (resultCode != RESULT_OK || data == null) {
            // Not asked again until the next launch, so a cancel always gets out.
            appendReport("  file choice cancelled\n");
            return;
        }
        List<Uri> uris = GameData.pickedUris(data);
        if (preLaunch && wantLossless) {
            takePreLaunchPick(null, uris);
            return;
        }
        if (preLaunch && wantUltimate) {
            takeUltimatePick(uris);
            return;
        }
        GameData.Picked picked = GameData.identify(this, uris);
        if (preLaunch) {
            takePreLaunchPick(picked, uris);
            return;
        }

        // Say what each file turned out to be. "No disc image found" on its own leaves
        // the user guessing whether they picked the wrong file or an unreadable one.
        StringBuilder message = new StringBuilder("PICKED\n");
        for (String verdict : picked.verdicts) {
            message.append("  ").append(verdict).append('\n');
        }
        if (picked.verdicts.isEmpty()) {
            message.append("  nothing\n");
        }
        appendReport(message.toString());

        boolean stillNeedImage = false;
        if (wantImage) {
            if (picked.imageRank == 2) {
                GameData.rememberImage(this, picked.imageUri);
                appendReport("GAME DATA\n  extracting " + picked.imageName + " to "
                        + GameData.destination(this).getName() + "\n");
                if (GameData.startExtraction(this, picked.imageUri)) {
                    pollExtraction();
                }
            } else {
                stillNeedImage = true;
            }
        }
        boolean stillNeedUpdate = wantUpdate && picked.titleUpdates.isEmpty();
        if (!picked.titleUpdates.isEmpty() && loadedWithoutUpdate) {
            appendReport("  the title was loaded before the update was here -"
                    + " restart the app to apply it\n");
        }
        // Ask again for whatever is still missing. Not a loop: cancelling the picker
        // stops it, and only a pick that misses re-asks.
        if (stillNeedImage || stillNeedUpdate) {
            askForFiles(stillNeedImage, stillNeedUpdate);
        }
    }

    /**
     * A file chosen from the pre-launch settings: whatever it turned out to be is taken -
     * a disc image extracted (or checked, if extracted already), a title update copied -
     * and the rows say what it was. Nothing is asked again; the rows are there.
     */
    private void takePreLaunchPick(GameData.Picked picked, List<Uri> uris) {
        if (wantLossless) {
            // Lossless.dll: the first file picked, checked as a Windows DLL and copied.
            String verdict = uris.isEmpty() ? "Nothing chosen."
                    : GameData.takeLossless(this, uris.get(0));
            pickNote = verdict;
            Log.i(TAG, "pre-launch: Lossless.dll - " + verdict);
            optionsPanel.setLaunchState(launchState());
            return;
        }
        StringBuilder note = new StringBuilder();
        for (String verdict : picked.verdicts) {
            appendReport("PICKED\n  " + verdict + "\n");
            if (note.length() > 0) {
                note.append("; ");
            }
            note.append(verdict.replace("\n    ", ": "));
        }
        if (picked.imageRank == 2) {
            GameData.rememberImage(this, picked.imageUri);
            if (GameData.startExtraction(this, picked.imageUri)) {
                pollExtraction();
            } else {
                note.append(note.length() > 0 ? "; " : "").append("the extraction did not start");
            }
        } else if (wantImage && picked.imageRank == 0 && picked.titleUpdates.isEmpty()) {
            note.append(note.length() > 0 ? ". " : "").append("Not the disc image");
        }
        if (!picked.titleUpdates.isEmpty()) {
            titleUpdate = GameData.titleUpdate(this);
        }
        pickNote = note.length() > 0 ? note + "." : "";
        Log.i(TAG, "pre-launch: picked - " + pickNote);
        optionsPanel.setLaunchState(launchState());
    }

    /**
     * PLAN 10.10: the Ultimate Edition's disc 2 chosen - checked by its own executable, then
     * every Live package on it installed as downloadable content on a thread of its own
     * (about 4.6 GB), the row showing the progress; the standalone disc's Awakening package,
     * if any, taken away after, disc 2's carrying the same add-in.
     */
    private void takeUltimatePick(List<Uri> uris) {
        if (ultimateInstalling) {
            return;
        }
        if (uris.isEmpty()) {
            pickNote = "Nothing chosen.";
        } else if (!GameData.isUltimateDisc2(this, uris.get(0))) {
            pickNote = "Not the Ultimate Edition's disc 2 (Additional Content Packs Install Disc).";
        } else {
            ultimateInstalling = true;
            pickNote = "";
            final Uri disc = uris.get(0);
            new Thread(() -> {
                String verdict = GameData.installDlc(GameActivity.this, disc);
                GameData.removeStandaloneAwakening(GameActivity.this);
                Log.i(TAG, "pre-launch: Ultimate Edition disc 2 - " + verdict);
                handler.post(() -> {
                    ultimateInstalling = false;
                    // PLAN 12.3: its Title Update 6 is in place now, if no 7 is.
                    titleUpdate = GameData.titleUpdate(GameActivity.this);
                    pickNote = verdict;
                    if (preLaunch) {
                        optionsPanel.setLaunchState(launchState());
                    }
                });
            }, "dlc-install").start();
            handler.post(new Runnable() {
                @Override
                public void run() {
                    if (!ultimateInstalling) {
                        return;
                    }
                    if (preLaunch) {
                        optionsPanel.setLaunchState(launchState());
                    }
                    handler.postDelayed(this, 500);
                }
            });
        }
        Log.i(TAG, "pre-launch: Ultimate Edition disc 2 - "
                + (pickNote.isEmpty() ? "installing" : pickNote));
        optionsPanel.setLaunchState(launchState());
    }

    /**
     * Polls progress twice a second.
     *
     * <p>Polling rather than a callback from native: a native thread calling into Java
     * has to attach to the JVM and then marshal to the main thread anyway, and this
     * way the extraction thread never touches JNI at all. Twice a second is plenty for
     * a job measured in minutes, and costs nothing.
     */
    /**
     * BUG-104: the test files in the app's files folder that change the game - a route
     * and its presses, the DLC hidden or limited, the GPU hacks (docs/TESTING.md) - by
     * name, or "" when there are none. The cheats are options, not these.
     */
    private String testFiles() {
        StringBuilder found = new StringBuilder();
        String[] names = getFilesDir().list();
        if (names == null) {
            return "";
        }
        java.util.Arrays.sort(names);
        for (String name : names) {
            boolean test = name.equals("route") || name.equals("route-call")
                    || name.equals("inject-pad") || name.equals("hide-dlc")
                    || name.equals("dlc-only") || name.startsWith("hack-");
            if (test && new java.io.File(getFilesDir(), name).isFile()) {
                found.append(found.length() == 0 ? "" : ", ").append(name);
            }
        }
        return found.toString();
    }

    /**
     * PLAN 12.8: Build chosen - the game's code made from the files in place, in
     * GameCodeService, the launcher following it.
     */
    private void startBuild() {
        GameData.TitleUpdate update = titleUpdate;
        if (update == null || update.number < 6 || GameCode.running(this)) {
            return;
        }
        gameCodeStarted = System.currentTimeMillis();
        Log.i(TAG, "launch: making the game's code (Title Update " + update.number + ")");
        GameCodeService.start(this);
        pollGameCode();
        if (preLaunch) {
            optionsPanel.setLaunchState(launchState());
        }
    }

    /** PLAN 12.5: the launcher refreshed with the making's progress while it goes on. */
    private void pollGameCode() {
        if (gameCodePolling) {
            return;
        }
        gameCodePolling = true;
        handler.post(new Runnable() {
            @Override
            public void run() {
                if (preLaunch) {
                    optionsPanel.setLaunchState(launchState());
                }
                if (GameCode.running(GameActivity.this)
                        || System.currentTimeMillis() - gameCodeStarted < 60_000) {
                    handler.postDelayed(this, 1000);
                } else {
                    gameCodePolling = false;
                    if (preLaunch) {
                        optionsPanel.setLaunchState(launchState());
                    }
                }
            }
        });
    }

    private void pollExtraction() {
        if (extractionPolling) {
            return;
        }
        extractionPolling = true;
        handler.post(new Runnable() {
            @Override
            public void run() {
                long[] progress = NativeBridge.extractProgress();
                setReport(NativeBridge.extractReport());
                if (preLaunch) {
                    optionsPanel.setLaunchState(launchState());
                }
                // States 1 and 2 are scanning and extracting; 3, 4 and 5 are
                // complete, failed and cancelled, all terminal.
                boolean running = progress != null && progress.length > 0
                        && progress[0] >= 1 && progress[0] <= 2;
                if (running) {
                    handler.postDelayed(this, 500);
                } else {
                    extractionPolling = false;
                    for (String line : NativeBridge.extractReport().split("\n", -1)) {
                        Log.i(TAG, "[report] " + line);
                    }
                    // Complete: the game starts now (bootWhenReady checks which ending).
                    bootWhenReady();
                }
            }
        });
    }

    private void appendReport(String extra) {
        padReportHeader = padReportHeader + "\n" + extra;
        report.setText(padReportHeader);
        for (String line : extra.split("\n", -1)) {
            Log.i(TAG, "[report] " + line);
        }
    }

    /** Replaces only the live tail of the overlay, keeping the static part. */
    private void setReport(String tail) {
        report.setText(padReportHeader + "\n" + tail);
    }

    /**
     * What the platform says about this display's colour and refresh capability.
     *
     * <p>Worth reporting next to the Vulkan side, because the two can disagree and the
     * disagreement is the interesting part. Wide colour gamut is a property of the
     * <em>window</em> on Android - requested via {@code android:colorMode} and granted
     * only if the display supports it - whereas in Vulkan it is a property of the
     * swapchain's colour space. A P3 panel reported as sRGB by Vulkan means the Vulkan
     * side is not asking, not that the panel cannot do it.
     */
    private String describeDisplay() {
        StringBuilder out = new StringBuilder("DISPLAY (platform)\n");
        android.view.Display display = getDisplay();
        if (display == null) {
            return out.append("  no display\n\n").toString();
        }
        out.append(String.format("  %-34s %d\n", "display id", display.getDisplayId()));
        out.append(String.format("  %-34s %s\n", "wide colour gamut supported",
                display.isWideColorGamut() ? "yes" : "no"));
        out.append(String.format("  %-34s %s\n", "window got wide colour gamut",
                getWindow().isWideColorGamut() ? "yes" : "no"));
        out.append(String.format("  %-34s %.3f Hz\n", "refresh rate",
                display.getRefreshRate()));

        android.view.Display.HdrCapabilities hdr = display.getHdrCapabilities();
        if (hdr == null || hdr.getSupportedHdrTypes().length == 0) {
            out.append(String.format("  %-34s none\n", "HDR types"));
        } else {
            StringBuilder types = new StringBuilder();
            for (int type : hdr.getSupportedHdrTypes()) {
                if (types.length() > 0) {
                    types.append(" ");
                }
                types.append(hdrTypeName(type));
            }
            out.append(String.format("  %-34s %s\n", "HDR types", types));
            // Read from the platform rather than hardcoded: feature 9.x has to
            // tonemap to this panel's actual luminance, not to a spec sheet's.
            out.append(String.format("  %-34s %.0f / %.0f nits\n", "HDR luminance max/min",
                    hdr.getDesiredMaxLuminance(), hdr.getDesiredMinLuminance()));
        }
        out.append('\n');
        // One line at a time, under the same "[report]" marker the native side uses,
        // so tools/appbringup/run.sh can reassemble a single report out of logcat
        // regardless of which side of the JNI boundary produced each line.
        for (String line : out.toString().split("\n", -1)) {
            Log.i(TAG, "[report] " + line);
        }
        return out.toString();
    }

    private static String hdrTypeName(int type) {
        switch (type) {
            case android.view.Display.HdrCapabilities.HDR_TYPE_DOLBY_VISION:
                return "DOLBY_VISION";
            case android.view.Display.HdrCapabilities.HDR_TYPE_HDR10:
                return "HDR10";
            case android.view.Display.HdrCapabilities.HDR_TYPE_HLG:
                return "HLG";
            case android.view.Display.HdrCapabilities.HDR_TYPE_HDR10_PLUS:
                return "HDR10_PLUS";
            default:
                return "type" + type;
        }
    }

    /**
     * Immersive fullscreen with the modern API. The deprecated
     * {@code SYSTEM_UI_FLAG_*} route is not used - it is a no-op on API 30+.
     */
    /** PLAN 13.1: what the update brings, before it is installed (or its page opened). */
    private void showReleaseNotes(boolean installs) {
        CharSequence notes = appUpdate.notes();
        Log.i(TAG, "update: the release notes shown (" + notes.length() + " characters)");
        new android.app.AlertDialog.Builder(this,
                android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("What's new in " + appUpdate.version())
                .setMessage(notes.length() > 0 ? notes : "This release has no notes.")
                .setPositiveButton(installs ? "Download & install" : "Open the release page",
                        (d, w) -> {
                            Log.i(TAG, "update: chosen after the notes");
                            appUpdate.choose();
                        })
                .setNegativeButton("Not now", null)
                .setOnDismissListener(d -> goFullscreen())
                .show();
    }

    private void goFullscreen() {
        getWindow().setDecorFitsSystemWindows(false);
        // Ask for the decor view first. getInsetsController() reads it and returns null
        // if it does not exist yet, so touching it here makes the ordering requirement
        // explicit rather than relying on the caller having set a content view.
        View decor = getWindow().getDecorView();
        decor.setKeepScreenOn(true);
        WindowInsetsController controller = getWindow().getInsetsController();
        if (controller == null) {
            Log.w(TAG, "no insets controller yet; staying windowed for now");
            return;
        }
        controller.hide(WindowInsets.Type.systemBars());
        controller.setSystemBarsBehavior(
                WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
    }

    // ---- the title's on-screen keyboard (a character's name) ----

    /** The dialog answering the title's keyboard prompt, while one is open. */
    private android.app.AlertDialog keyboardDialog;

    /**
     * Polls for the title's keyboard prompt four times a second once the guest runs.
     *
     * <p>Polling, as for extraction: native never calls into Java, and a prompt that
     * waits a quarter of a second for its dialog is not noticed.
     */
    private void pollKeyboard() {
        handler.postDelayed(new Runnable() {
            @Override
            public void run() {
                // The app's end, polled here too (task 4.9): the title ended itself, or
                // the graphics device was lost.
                String exit = NativeBridge.exitRequest();
                if (!exit.isEmpty()) {
                    handleExit(exit);
                    return;
                }
                if (keyboardDialog == null) {
                    String[] prompt = NativeBridge.keyboardRequest();
                    if (prompt != null && prompt.length == 4) {
                        showKeyboard(prompt[0], prompt[1], prompt[2], prompt[3]);
                    }
                }
                handler.postDelayed(this, 250);
            }
        }, 250);
    }

    /**
     * A text dialog for the prompt: the title's own title and description, its default
     * text selected so typing replaces it, its length limit, and the system keyboard
     * shown at once. OK (or the keyboard's Done) answers with the text, Cancel or Back
     * cancels; either way the title's call completes and the dialog closes.
     */
    private void showKeyboard(String title, String description, String defaultText,
                              String maxChars) {
        int limit = 1;
        try {
            limit = Math.max(1, Integer.parseInt(maxChars));
        } catch (NumberFormatException e) {
            Log.w(TAG, "keyboard: bad length " + maxChars);
        }
        final android.widget.EditText field = new android.widget.EditText(this);
        field.setSingleLine(true);
        field.setText(defaultText);
        field.setSelectAllOnFocus(true);
        field.setFilters(new android.text.InputFilter[] {
                new android.text.InputFilter.LengthFilter(limit)});
        // Done on the keyboard, and the keyboard under the dialog rather than the
        // landscape full-screen editor, which would hide the title's prompt.
        field.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE
                | android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN);
        FrameLayout padding = new FrameLayout(this);
        int margin = (int) (20 * getResources().getDisplayMetrics().density);
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT);
        params.leftMargin = margin;
        params.rightMargin = margin;
        padding.addView(field, params);

        final boolean[] answered = {false};
        keyboardDialog = new android.app.AlertDialog.Builder(this)
                .setTitle(title.isEmpty() ? "Enter text" : title)
                .setMessage(description.isEmpty() ? null : description)
                .setView(padding)
                .setPositiveButton(android.R.string.ok, (dialog, which) -> {
                    answered[0] = true;
                    NativeBridge.keyboardAnswer(field.getText().toString(), true);
                })
                .setNegativeButton(android.R.string.cancel, (dialog, which) -> {
                    answered[0] = true;
                    NativeBridge.keyboardAnswer("", false);
                })
                .setOnCancelListener(dialog -> {
                    answered[0] = true;
                    NativeBridge.keyboardAnswer("", false);
                })
                .create();
        keyboardDialog.setOnDismissListener(dialog -> {
            if (!answered[0]) {
                NativeBridge.keyboardAnswer("", false);
            }
            keyboardDialog = null;
            goFullscreen();
        });
        field.setOnEditorActionListener((view, actionId, event) -> {
            // The on-screen keyboard's Done, or Enter on a hardware keyboard (which
            // arrives as no action with the key event).
            boolean done = actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE
                    || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER
                        && event.getAction() == KeyEvent.ACTION_DOWN);
            if (done) {
                answered[0] = true;
                NativeBridge.keyboardAnswer(field.getText().toString(), true);
                keyboardDialog.dismiss();
                return true;
            }
            return false;
        });
        if (keyboardDialog.getWindow() != null) {
            keyboardDialog.getWindow().setSoftInputMode(
                    WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        }
        keyboardDialog.show();
        field.requestFocus();
        Log.i(TAG, "keyboard: \"" + title + "\" shown, default \"" + defaultText + "\"");
    }
}
