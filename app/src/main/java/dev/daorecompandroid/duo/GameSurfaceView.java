package dev.daorecompandroid.duo;

import android.content.Context;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * The window the game presents to (task 4.2).
 *
 * <p>A plain {@link SurfaceView}, not {@code androidx.games:games-activity}'s
 * GameActivity. That library would supply a native window and native input in one
 * package, which is appealing, but two things here argue against it:
 *
 * <ul>
 *   <li>The glyph system (Phase 7) has to identify which controller is attached -
 *       vendor id, product id, device name - to choose between Xbox, Nintendo and
 *       PlayStation face-button layouts. That information comes from
 *       {@code android.view.InputDevice}, a Java API. So a Java-side input path is
 *       needed regardless, and GameActivity's native input buffer would be a second
 *       path to keep in step with it.</li>
 *   <li>{@code SurfaceHolder.Callback} maps exactly onto what Xenia's presenter wants -
 *       create, resize, destroy, all on the UI thread - so there is nothing to adapt.</li>
 * </ul>
 *
 * <p>The surface is not created until this view is attached and laid out, so nothing
 * here may assume a window exists.
 */
final class GameSurfaceView extends SurfaceView implements SurfaceHolder.Callback {

    private static final String TAG = "Warden";

    /** Whether the native side currently holds a window. Main thread only. */
    private boolean nativeHasSurface;

    GameSurfaceView(Context context) {
        super(context);
        getHolder().addCallback(this);
        // Deliberately NO setBackgroundColor here. A SurfaceView that is not
        // Z-ordered on top shows its surface on a separate layer *below* the window,
        // and punches a transparent hole in the window to let it through. A View
        // background is drawn into the window layer - that is, straight over the hole -
        // so setting one hides everything the renderer presents, permanently and
        // silently. The activity's windowBackground already covers the gap before the
        // first frame.
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.i(TAG, "surfaceCreated");
        NativeBridge.surfaceCreated(holder.getSurface());
        nativeHasSurface = true;
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "surfaceChanged " + width + "x" + height + " format=" + format);
        if (!nativeHasSurface) {
            // Defensive: the documented order is created-then-changed, but a resize
            // arriving without a window would otherwise be handed to a null one.
            NativeBridge.surfaceCreated(holder.getSurface());
            nativeHasSurface = true;
        }
        NativeBridge.surfaceChanged(width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "surfaceDestroyed");
        if (!nativeHasSurface) {
            return;
        }
        nativeHasSurface = false;
        // Blocks. Android frees the buffers behind this Surface as soon as we return,
        // so the native side has to have let go first - see NativeBridge's contract.
        NativeBridge.surfaceDestroyed();
    }

    /** Whether the native side has a window right now. Main thread only. */
    boolean hasSurface() {
        return nativeHasSurface;
    }
}
