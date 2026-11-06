package com.fusionautomate.aanav

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.collect

/**
 * Foreground service that keeps the process alive, owns the BLE link, and
 * forwards parsed navigation updates plus a 5 s heartbeat to the display.
 */
class NavForegroundService : Service() {

    private var job: Job? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        startForeground(NOTIF_ID, buildNotification())
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (job == null) {
            BleLink.start(applicationContext)
            job = CoroutineScope(Dispatchers.IO).launch {
                val heartbeat = launch {
                    while (isActive) {
                        delay(5000)
                        if (BleLink.connected) {
                            BleLink.send("{\"t\":\"ping\"}\n")
                        }
                    }
                }
                NavBus.updates.collect { update ->
                    if (BleLink.connected) {
                        BleLink.send(update.toJson())
                    }
                }
                heartbeat.cancel()
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        job?.cancel()
        job = null
        BleLink.stop()
        super.onDestroy()
    }

    private fun buildNotification(): Notification {
        val pi = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "Navigation link", NotificationManager.IMPORTANCE_LOW)
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("AA Nav Display link")
            .setContentText("Relaying Google Maps navigation over Bluetooth")
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "nav_link"
        private const val NOTIF_ID = 1

        fun start(context: Context) {
            ContextCompat.startForegroundService(
                context, Intent(context, NavForegroundService::class.java)
            )
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, NavForegroundService::class.java))
        }
    }
}
