package com.fusionautomate.aanav

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log

/**
 * Reads the live Google Maps navigation notification and forwards parsed
 * turn-by-turn updates to [NavBus]. Requires the user to grant notification
 * access once (Settings > Special app access > Notification access).
 */
class NavNotificationListener : NotificationListenerService() {

    companion object {
        private const val TAG = "NavListener"
        private const val MAPS_PACKAGE = "com.google.android.apps.maps"

        /** True while the system has connected this listener. */
        @Volatile
        var active = false
            private set
    }

    override fun onListenerConnected() {
        active = true
        Log.i(TAG, "notification listener connected")
    }

    override fun onListenerDisconnected() {
        active = false
        Log.i(TAG, "notification listener disconnected")
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (!active) return
        if (sbn.packageName != MAPS_PACKAGE) return

        val extras = sbn.notification?.extras ?: return
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString()
        val text = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString()
        val bigText = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()

        try {
            val update = NavParser.parse(title, text, bigText) ?: return
            NavBus.emit(update)
        } catch (e: Exception) {
            Log.w(TAG, "parse failed: ${e.message}")
        }
    }
}
