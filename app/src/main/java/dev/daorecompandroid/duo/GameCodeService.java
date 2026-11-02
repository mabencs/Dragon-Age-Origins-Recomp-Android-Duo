package dev.daorecompandroid.duo;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

/**
 * PLAN 12.5: makes the game's code ({@link GameCode#make}) in a process of its own
 * (":gamecode", AndroidManifest.xml), in the foreground, holding the CPU awake - about ten
 * minutes on the Thor, which a player may well spend with the app put away or the screen
 * off. It reports through {@code files/gamecode/state.txt}, which the launcher reads, and
 * ends itself when the code is made or the making failed. Started again, it carries on:
 * every file already compiled is kept.
 */
public final class GameCodeService extends Service {
    private static final String TAG = "WardenGameCode";
    private static final String CHANNEL = "gamecode";
    private static final int NOTIFICATION = 12;

    private Thread worker;

    static void start(Context context) {
        context.startForegroundService(new Intent(context, GameCodeService.class));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(CHANNEL,
                "Making the game's code", NotificationManager.IMPORTANCE_LOW));
        Notification notification = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle("Making the game's code")
                .setContentText("From your disc, for this device - once, about ten minutes")
                .setOngoing(true)
                .build();
        startForeground(NOTIFICATION, notification);
        if (worker == null || !worker.isAlive()) {
            worker = new Thread(this::work, "gamecode");
            worker.start();
        }
        return START_NOT_STICKY;
    }

    private void work() {
        PowerManager power = getSystemService(PowerManager.class);
        PowerManager.WakeLock awake = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,
                "warden:gamecode");
        awake.acquire(60 * 60 * 1000L);
        GameCode.Progress progress = new GameCode.Progress(this);
        // The launcher's sign that this process is alive (GameCode.running): the state
        // file touched every ten seconds, through the one long compile too.
        java.util.concurrent.ScheduledExecutorService heartbeat =
                java.util.concurrent.Executors.newSingleThreadScheduledExecutor();
        heartbeat.scheduleAtFixedRate(progress::touch, 10, 10,
                java.util.concurrent.TimeUnit.SECONDS);
        try {
            GameCode.make(this, progress);
        } catch (GameCode.Failure failure) {
            Log.e(TAG, failure.getMessage());
            progress.report(GameCode.Progress.FAILED, 0, 1, failure.getMessage());
        } finally {
            heartbeat.shutdownNow();
            if (awake.isHeld()) {
                awake.release();
            }
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
