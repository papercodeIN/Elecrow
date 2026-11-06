package com.fusionautomate.aanav

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

/**
 * Minimal setup screen: grant notification access, grant runtime permissions,
 * start/stop the link service. All UI is built programmatically - there is
 * nothing to configure by hand.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val pad = (16 * resources.displayMetrics.density).toInt()
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }

        status = TextView(this).apply { text = getString(R.string.status_intro) }
        layout.addView(status)

        layout.addView(Button(this).apply {
            text = getString(R.string.btn_notif_access)
            setOnClickListener {
                startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
            }
        })

        layout.addView(Button(this).apply {
            text = getString(R.string.btn_start)
            setOnClickListener {
                requestNeededPermissions()
                NavForegroundService.start(this@MainActivity)
                status.text = getString(R.string.status_running)
            }
        })

        layout.addView(Button(this).apply {
            text = getString(R.string.btn_stop)
            setOnClickListener {
                NavForegroundService.stop(this@MainActivity)
                status.text = getString(R.string.status_stopped)
            }
        })

        setContentView(layout)
    }

    private fun requestNeededPermissions() {
        val wanted = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31 &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
            != PackageManager.PERMISSION_GRANTED
        ) {
            wanted.add(Manifest.permission.BLUETOOTH_CONNECT)
        }
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED
        ) {
            wanted.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        if (Build.VERSION.SDK_INT <= 30 &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED
        ) {
            wanted.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (wanted.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, wanted.toTypedArray(), 1)
        }
    }
}
