package dev.daorecompandroid.duo;

import android.hardware.input.InputManager;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/**
 * Controller discovery and event routing (task 4.6).
 *
 * <p>Two jobs, kept together because they share the device list:
 *
 * <ol>
 *   <li><b>Discovery.</b> Which gamepads exist, and what each one says it is - vendor
 *       id, product id, name, descriptor. This is the data the glyph system (Phase 7)
 *       needs to choose between Xbox, Nintendo and PlayStation face-button layouts,
 *       and it is only available through {@link InputDevice}, a Java API. That is the
 *       main reason this app does not use {@code androidx.games:games-activity}, whose
 *       selling point is handling input natively.</li>
 *   <li><b>Routing.</b> Key and motion events go straight to native, which maps them
 *       onto the Xbox 360 gamepad layout the recompiled game expects. The mapping is a
 *       table and belongs on the side that owns the struct; Java does not get a vote on
 *       which bit is {@code X_INPUT_GAMEPAD_A}.</li>
 * </ol>
 *
 * <p>Slots: the game sees up to four controllers, as the 360 did. A slot is assigned on
 * first sight and kept until the device disappears, so unplugging controller 2 does not
 * renumber controller 3 underneath the player.
 */
final class ControllerManager implements InputManager.InputDeviceListener {

    private static final String TAG = "Warden";
    static final int MAX_PADS = 4;

    private final InputManager inputManager;
    /** deviceId per slot, or -1. Index is the slot the game sees. */
    private final int[] slotDeviceIds = new int[MAX_PADS];

    ControllerManager(InputManager inputManager) {
        this.inputManager = inputManager;
        for (int i = 0; i < MAX_PADS; i++) {
            slotDeviceIds[i] = -1;
        }
    }

    void start() {
        inputManager.registerInputDeviceListener(this, null);
        for (int deviceId : InputDevice.getDeviceIds()) {
            onInputDeviceAdded(deviceId);
        }
    }

    void stop() {
        inputManager.unregisterInputDeviceListener(this);
    }

    /**
     * Whether this device is something a player holds.
     *
     * <p>Checked with {@code (sources & mask) == mask} rather than {@code != 0}: a
     * source is a bitfield of class plus specific bits, so a plain non-zero test
     * matches far too much - a touchscreen reports {@code SOURCE_CLASS_POINTER} and
     * would pass.
     */
    private static boolean isGamepad(InputDevice device) {
        if (device == null || device.isVirtual()) {
            return false;
        }
        int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /** The game's slot of this device, or -1 when it is not a pad of the game's. */
    int slotOf(int deviceId) {
        // -1 marks an empty slot, and it is also the virtual keyboard's id
        // (KeyCharacterMap.VIRTUAL_KEYBOARD): the system's Back - the gesture and the
        // navigation bar - and keys injected with `adb shell input`. Matching it took them
        // for presses on a pad that is not there, so Back never reached the activity
        // (BUG-053).
        if (deviceId < 0) {
            return -1;
        }
        for (int i = 0; i < MAX_PADS; i++) {
            if (slotDeviceIds[i] == deviceId) {
                return i;
            }
        }
        return -1;
    }

    /**
     * The Thor's built-in pad ("Odin Controller", AYN's vendor 0x2020 product 0x0111): its
     * face buttons are printed in Nintendo's arrangement (A right, B bottom, X top, Y
     * left), and with AYN's controller setting at its default ({@code
     * persist.sys.gamepad.type} 0) its firmware reports them by those labels - the bottom
     * button as BUTTON_B - where Android's convention, and the native side's positional
     * table (warden_input.cc), take BUTTON_A for the bottom one. Seen in play on
     * 2026-10-30: with the Nintendo prompts, the bottom button (B, shown as the game's
     * confirm) cancelled and the right one (A) confirmed. So for this pad in that mode A
     * and B trade, and X and Y, before anything reads the key - the game, the options
     * panel - and the bottom button is the game's A whatever it is called, as the prompts
     * draw it. Another value of the property is taken for AYN's other mode, by position
     * (not yet seen).
     */
    private static boolean reportsByLabel(InputDevice device) {
        if (device.getVendorId() != 0x2020 || device.getProductId() != 0x0111) {
            return false;
        }
        String type = systemProperty("persist.sys.gamepad.type");
        return type.isEmpty() || type.equals("0");
    }

    private static String systemProperty(String name) {
        try {
            Process process = Runtime.getRuntime().exec(new String[] {"getprop", name});
            try (java.io.BufferedReader reader = new java.io.BufferedReader(
                    new java.io.InputStreamReader(process.getInputStream()))) {
                String line = reader.readLine();
                return line == null ? "" : line.trim();
            }
        } catch (java.io.IOException e) {
            return "";
        }
    }

    /** Devices whose face buttons arrive by label (reportsByLabel), by id. */
    private final java.util.Set<Integer> labelDevices = new java.util.HashSet<>();

    /**
     * The event as the bottom-is-A convention has it: for a pad that reports its face
     * buttons by label, A and B traded, and X and Y; any other event as it is.
     */
    KeyEvent positional(KeyEvent event) {
        if (!labelDevices.contains(event.getDeviceId())) {
            return event;
        }
        int code = event.getKeyCode();
        int swapped = code == KeyEvent.KEYCODE_BUTTON_A ? KeyEvent.KEYCODE_BUTTON_B
                : code == KeyEvent.KEYCODE_BUTTON_B ? KeyEvent.KEYCODE_BUTTON_A
                : code == KeyEvent.KEYCODE_BUTTON_X ? KeyEvent.KEYCODE_BUTTON_Y
                : code == KeyEvent.KEYCODE_BUTTON_Y ? KeyEvent.KEYCODE_BUTTON_X
                : code;
        if (swapped == code) {
            return event;
        }
        return new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(),
                swapped, event.getRepeatCount(), event.getMetaState(), event.getDeviceId(),
                event.getScanCode(), event.getFlags(), event.getSource());
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        InputDevice device = InputDevice.getDevice(deviceId);
        if (!isGamepad(device) || slotOf(deviceId) >= 0) {
            return;
        }
        if (reportsByLabel(device)) {
            labelDevices.add(deviceId);
            Log.i(TAG, "pad " + deviceId + " reports its face buttons by label: A and B, X"
                    + " and Y traded so the bottom button is the game's A");
        } else {
            labelDevices.remove(deviceId);
        }
        int slot = -1;
        for (int i = 0; i < MAX_PADS; i++) {
            if (slotDeviceIds[i] < 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            Log.w(TAG, "no free pad slot for device " + deviceId);
            return;
        }
        slotDeviceIds[slot] = deviceId;

        // Analog triggers are not universal: some pads report them as buttons only.
        // Reported so the native side knows whether a trigger value is meaningful.
        boolean hasAnalogTriggers =
                device.getMotionRange(MotionEvent.AXIS_LTRIGGER, InputDevice.SOURCE_JOYSTICK) != null
                        || device.getMotionRange(MotionEvent.AXIS_BRAKE, InputDevice.SOURCE_JOYSTICK) != null;
        boolean hasHat =
                device.getMotionRange(MotionEvent.AXIS_HAT_X, InputDevice.SOURCE_JOYSTICK) != null;

        Log.i(TAG, String.format(
                "pad slot %d: id=%d vendor=0x%04X product=0x%04X analogTriggers=%b hat=%b \"%s\"",
                slot, deviceId, device.getVendorId(), device.getProductId(),
                hasAnalogTriggers, hasHat, device.getName()));

        NativeBridge.padAttached(slot, deviceId, device.getVendorId(),
                device.getProductId(), device.getName(), device.getDescriptor(),
                hasAnalogTriggers, hasHat);
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        int slot = slotOf(deviceId);
        if (slot < 0) {
            return;
        }
        slotDeviceIds[slot] = -1;
        Log.i(TAG, "pad slot " + slot + " removed (id=" + deviceId + ")");
        NativeBridge.padDetached(slot);
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        // A changed device may have gained or lost gamepad sources. Re-evaluate from
        // scratch rather than trying to work out what changed.
        onInputDeviceRemoved(deviceId);
        onInputDeviceAdded(deviceId);
    }

    /** @return true if the event belonged to a pad and was consumed. */
    boolean onKey(KeyEvent event) {
        int slot = slotOf(event.getDeviceId());
        if (slot < 0) {
            return false;
        }
        // Repeats carry no new information - the button is already held - and feeding
        // them through would make a held button look like rapid presses to anything
        // counting edges.
        if (event.getRepeatCount() > 0) {
            return true;
        }
        boolean down = event.getAction() == KeyEvent.ACTION_DOWN;
        if (!down && event.getAction() != KeyEvent.ACTION_UP) {
            return false;
        }
        return NativeBridge.padKey(slot, event.getKeyCode(), down);
    }

    /** @return true if the event belonged to a pad and was consumed. */
    boolean onMotion(MotionEvent event) {
        int slot = slotOf(event.getDeviceId());
        if (slot < 0 || event.getAction() != MotionEvent.ACTION_MOVE) {
            return false;
        }
        NativeBridge.padAxes(slot,
                event.getAxisValue(MotionEvent.AXIS_X),
                event.getAxisValue(MotionEvent.AXIS_Y),
                event.getAxisValue(MotionEvent.AXIS_Z),
                event.getAxisValue(MotionEvent.AXIS_RZ),
                triggerValue(event, MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE),
                triggerValue(event, MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS),
                event.getAxisValue(MotionEvent.AXIS_HAT_X),
                event.getAxisValue(MotionEvent.AXIS_HAT_Y));
        return true;
    }

    /**
     * Triggers come in on either axis depending on the driver: {@code AXIS_LTRIGGER} /
     * {@code AXIS_RTRIGGER} on some pads, {@code AXIS_BRAKE} / {@code AXIS_GAS} on
     * others. Taking the larger of the two is correct because the unused one reads 0,
     * and it avoids having to know which family a given pad belongs to.
     */
    private static float triggerValue(MotionEvent event, int primaryAxis, int altAxis) {
        return Math.max(event.getAxisValue(primaryAxis), event.getAxisValue(altAxis));
    }
}
