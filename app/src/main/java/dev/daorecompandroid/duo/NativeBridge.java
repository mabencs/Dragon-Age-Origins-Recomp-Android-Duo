package dev.daorecompandroid.duo;

import android.view.Surface;

/**
 * The entire JNI surface area of the app (task 4.10).
 *
 * <p>Kept deliberately small, and kept in one file so it stays that way. Every method
 * here is a place where a Java-side mistake becomes a native crash with a stack trace
 * nobody can read, so the rule is: one call per lifecycle event, no call that returns a
 * native pointer into Java, and no call that Java makes more than once per event.
 *
 * <p>Threading contract, which the native side relies on:
 * <ul>
 *   <li>{@link #startup} and {@link #shutdown} are called once each, from the Android
 *       main thread.</li>
 *   <li>The three surface methods are called from the Android main thread only, in the
 *       order the {@code SurfaceHolder} callbacks arrive. The main thread is what
 *       Xenia's presenter calls "the UI thread".</li>
 *   <li>{@link #surfaceDestroyed} does not return until the native side has stopped
 *       touching the {@code ANativeWindow}. Android reclaims the buffers the moment the
 *       callback returns, so returning early is a use-after-free, not a race that
 *       usually works.</li>
 * </ul>
 */
final class NativeBridge {

    static {
        System.loadLibrary("warden");
    }

    private NativeBridge() {
    }

    /**
     * Creates the native runtime and brings up a Vulkan device.
     *
     * @param internalDataDir app-private storage, for logs and caches. Passed in rather
     *                        than discovered natively because the native side has no
     *                        Context and the paths are not guaranteed by anything else.
     * @return true if a Vulkan device suitable for Xenos emulation was created.
     */
    static native boolean startup(String internalDataDir);

    /** Tears the runtime down. Safe to call even if {@link #startup} failed. */
    static native void shutdown();

    /**
     * Hands over the window to present to. The native side takes its own
     * {@code ANativeWindow} reference; the {@link Surface} is not retained.
     */
    static native void surfaceCreated(Surface surface);

    /** New size in physical pixels. Also fires once right after creation. */
    static native void surfaceChanged(int width, int height);

    /** Blocks until the native side has released the window. */
    static native void surfaceDestroyed();

    /** Whether the game should be running. Drives the emulation thread, not the surface. */
    static native void setPaused(boolean paused);

    /**
     * What the Vulkan device turned out to be, as human-readable lines.
     *
     * <p>This is the in-app equivalent of {@code tools/vkdevice/run.sh}, which proved
     * the device works from a command-line binary. Running the same checks inside the
     * APK is what proves it works with an app's library loading, its SELinux domain and
     * a real SurfaceView window - which is a different claim.
     */
    static native String deviceReport();

    // ---- controllers (task 4.6) ----
    //
    // Called from the Android main thread, like everything else here. The mapping from
    // Android key codes and axes onto the Xbox 360 gamepad layout lives on the native
    // side, because that is where the X_INPUT_GAMEPAD struct the game reads lives.

    /**
     * A pad appeared in {@code slot} (0-3, as the 360 numbered them).
     *
     * @param vendorId          USB/Bluetooth vendor id, or 0 if unknown
     * @param productId         product id, or 0 if unknown
     * @param descriptor        stable per-device string; survives reconnection, unlike
     *                          the device id, so it is what settings should key on
     * @param hasAnalogTriggers false if the pad reports its triggers as buttons only,
     *                          in which case a trigger value of 0 or 255 is all there is
     */
    static native void padAttached(int slot, int deviceId, int vendorId, int productId,
                                   String name, String descriptor,
                                   boolean hasAnalogTriggers, boolean hasHat);

    static native void padDetached(int slot);

    /**
     * PLAN 10.17: the on-screen controller's state in the 360's terms - the button bits,
     * the sticks -1..1 with up positive, the triggers 0..1 - merged into the first pad.
     * Inactive, it is ignored and the first pad is only what is held.
     */
    /**
     * PLAN 10.13: a mod made for the PC version converted to the 360's formats, in place
     * (DAORecomp's converter; its PC files kept in mods/.originals/). The report: a
     * "result&lt;tab&gt;&lt;converted 0|1&gt;&lt;tab&gt;&lt;files&gt;" line, then an "addin" line for each
     * add-in installed as downloadable content in contentDir (PLAN 10.19: title, package
     * folder, files), then "problem", "left-out" and "note" lines. Slow: never on the UI
     * thread.
     */
    static native String convertMod(String modFolder, String gameRoot, String contentDir);

    /**
     * PLAN 10.19: a .7z or .rar archive unpacked into a folder (entries outside it
     * refused). "" when done, else why not. Not on the UI thread.
     */
    static native String unpackArchive(String archive, String into);

    /** PLAN 9.20: photo mode on or off at the game's next frame (exploring or fighting). */
    static native void photoToggle();

    /**
     * PLAN 9.20: {on, field of view (horizontal degrees), roll (degrees), speed, hints
     * shown, photos asked for so far} - photo mode's state for its overlay.
     */
    static native float[] photoState();

    static native void touchPad(boolean active, int buttons, float leftX, float leftY,
                                float rightX, float rightY, float leftTrigger,
                                float rightTrigger);

    /** @return true if this key code maps to a 360 button and was consumed. */
    static native boolean padKey(int slot, int keyCode, boolean down);

    /** Axis values as Android reports them: sticks -1..1, triggers 0..1. */
    static native void padAxes(int slot, float leftX, float leftY, float rightX,
                               float rightY, float leftTrigger, float rightTrigger,
                               float hatX, float hatY);

    /** The pads seen so far and their live state, for the bring-up report. */
    static native String padReport();

    // ---- game data extraction (task 4.4) ----

    /**
     * Starts extracting the disc image, on a native thread.
     *
     * @param isoFd         a readable, seekable descriptor. <b>Native takes ownership
     *                      and closes it</b>, so the caller must have detached it.
     * @param destinationDir where to write; created if absent
     * @return false if an extraction is already running
     */
    static native boolean extractBegin(int isoFd, String destinationDir);

    /**
     * Progress, as {@code {state, bytesDone, bytesTotal, filesDone, filesTotal,
     * filesSkipped}}. State matches {@code Extractor::State}: 0 idle, 1 scanning,
     * 2 extracting, 3 complete, 4 failed, 5 cancelled.
     *
     * <p>One array rather than six calls, so the numbers in it describe the same
     * instant - polling them separately would let a progress bar show a byte count from
     * after the file count it is divided by.
     */
    /**
     * PLAN 10.10: installs downloadable content - a Live package (LIVE/PIRS), or every one
     * in a disc image (Ultimate Edition's disc 2) - into {@code contentDir}. A disc image's
     * title update for this game's disc (disc 2 carries Title Update 6, PLAN 12.3) is
     * copied whole to {@code titleUpdatePath} (empty for none). Slow for large packages:
     * call it off the UI thread. Native owns and closes {@code fd}.
     *
     * @return what happened, in words for the launch screen
     */
    static native String installDlc(int fd, String contentDir, String titleUpdatePath);

    /**
     * PLAN 12.5: the title update package's default.xexp written to {@code outPath}, for
     * making the game's code ({@link GameCode}). False if it is not a package holding one.
     */
    static native boolean titleUpdatePatch(String packagePath, String outPath);

    /** PLAN 10.10: the DLC install's bytes done and in all ({@code [done, total]}). */
    static native long[] dlcProgress();

    static native long[] extractProgress();

    static native void extractCancel();

    /** Whether {@code destinationDir} already holds a finished extraction. */
    static native boolean extractIsComplete(String destinationDir);

    /**
     * Loads the title from the extracted game data, once per process - the button
     * prompts' set, the intro logos and the title update are taken now. Called as the
     * game is launched, so what the pre-launch settings chose is what it starts with.
     */
    static native void loadTitle(String gameDataDir);

    // ---- the second screen's deck (PLAN 8, warden_deck.h) ----

    /**
     * The state mirror's latest snapshot: one "path&lt;tab&gt;value" line for each token path the
     * game's UI observes, with the text it was last shown. Never waits on the game.
     */
    static native String deckSnapshot();

    /** {now ns, the game's swaps so far, presents so far, its last frame intervals ns...}. */
    static native long[] deckFrames();

    /**
     * PLAN 8.8: a tap on the deck's quickbar - quick slot 0-5 pressed as its own button
     * press (3-5 with RT). Returns "" when queued, else why not (the game not exploring or
     * fighting, a press still being made).
     */
    static native String deckQuickslot(int slot);

    /**
     * What the deck has open: read patterns, one a line. Their long texts join the snapshot
     * while they are asked for (warden_deck.h).
     */
    static native void deckWatch(String patterns);

    /**
     * The Items tab's buttons, for the character the game has selected: DeckSource's
     * ITEM_EQUIP, ITEM_USE (its `ability`) or ITEM_UNEQUIP on `item`. Returns "" when queued,
     * else why not.
     */
    static native String deckItem(int action, int item, int ability);

    /**
     * The deck's faces: each party slot's face generation (0 none yet), changed when the
     * game redraws that portrait (read back from the GPU while the deck pulls).
     */
    static native int[] deckPortraitGenerations();

    /**
     * What the game's frames go through as it runs - frame generation, the upscaler, the
     * rendered and the shown size - as "Render.&lt;name&gt;&lt;tab&gt;value" lines.
     */
    static native String deckRender();

    /** A party slot's face: 146x146 RGBA, premultiplied, or null. */
    static native byte[] deckPortrait(int slot);

    /** Human-readable extraction state, for the report. */
    static native String extractReport();

    /**
     * Identifies a disc image, as {@code {valid, titleId, mediaId, rank}}.
     *
     * <p><b>Native takes ownership of {@code fd} and closes it</b>, as it does for
     * every descriptor crossing this boundary. One rule, so there is never a question
     * of who closes what.
     *
     * <p>Sort candidates on {@code rank} and take the highest: 2 is Dragon Age:
     * Origins, 1 is another Dragon Age disc, 0 is something else. The rank exists so
     * that which title and media ids mean what stays in {@code warden_title.h} rather
     * than being restated here, where it could drift.
     */
    static native int[] imageIdentity(int fd);

    /**
     * Starts guest code, on a native thread of its own: the game runs from here.
     *
     * @return false if the guest is not ready - no title loaded, an image that is not
     *     the linked build's, the GPU not started; {@link #bootReport} says which - or
     *     was already started
     */
    static native boolean bootGuest();

    /**
     * The title image's and the guest boot's sections of the report: why the game did
     * not start, when {@link #bootGuest} says it did not.
     */
    static native String bootReport();

    // ---- the title's on-screen keyboard (a character's name) ----
    //
    // The title's call files a prompt and waits; the main thread polls for it and
    // answers, so native never calls into Java.

    /**
     * The prompt the title is waiting on, as {title, description, default text, the
     * maximum number of characters}, or null when there is none.
     */
    static native String[] keyboardRequest();

    /** Answers the waiting prompt: the text when accepted, a cancel otherwise. */
    static native void keyboardAnswer(String text, boolean accepted);

    // ---- the app's end (task 4.9), polled with the keyboard ----

    /**
     * "" while the app should run; otherwise "quit:" or "error:" and a reason - the
     * title ended itself, or the graphics device was lost. The process has to end then:
     * the runtime cannot be started twice in one process.
     */
    static native String exitRequest();

    /** The player quit: recorded like the title's own end, for the log. */
    static native void requestQuit(String why);

    // ---- the display (PLAN 9.2) ----

    /**
     * The display's peak in nits when it takes HDR10 ({@code Display.HdrCapabilities}),
     * 0 when it does not: the native side offers HDR10 output only then.
     */
    static native void displayHdr(float maxNits);

    // ---- the options panel (PLAN 10.1) ----

    /**
     * The options panel is open: the game holds - its threads, clocks and sound - as when
     * the app is put away, but without the autosave that can ask for.
     */
    static native void holdGame(boolean held);

    /**
     * PLAN 11.1: a bug report's fresh save, the game's own autosave whatever the option
     * says - 1 written, 0 none possible now (not exploring, or held), -1 not in 6 s.
     * Blocks: never on the UI thread.
     */
    static native int saveForReport();

    /**
     * An option's file was just written: the native side takes the new value at once if
     * the game can take it while running ({@link Options.Option#live}).
     *
     * @return true if it was taken; false if it holds from the next start.
     */
    static native boolean applyOption(String name, String value);

    // ---- the audio route (PLAN 9.12) ----

    /**
     * The ids ({@code AudioDeviceInfo.getId}) of the output devices that are headphones,
     * whenever the set changes: while the sound plays on one of them, and Settings ->
     * Headphones asks for it, the native side makes the headphone surround instead of the
     * speakers' mixdown.
     */
    static native void audioHeadphones(int[] deviceIds);

    /** BUG-109: Android's media volume as a linear gain, applied to the game's sound. */
    static native void audioMediaVolume(float gain);
}
