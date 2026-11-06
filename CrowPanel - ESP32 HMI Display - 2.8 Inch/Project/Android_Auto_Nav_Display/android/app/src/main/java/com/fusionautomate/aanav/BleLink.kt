package com.fusionautomate.aanav

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancelChildren
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID
import kotlin.coroutines.resume

/**
 * BLE GATT client for the AA Nav Display (Nordic UART Service).
 * Scans for the display by name, connects, subscribes to TX notifications,
 * and auto-reconnects with retries until [stop] is called.
 *
 * Uses the classic (deprecated on API 33+) characteristic APIs so a single
 * code path covers Android 8 through 14; suppressions are intentional.
 */
@SuppressLint("MissingPermission") // callers ensure BLUETOOTH_CONNECT is granted
object BleLink {

    private const val TAG = "BleLink"
    private const val DEVICE_NAME = "AA Nav Display"

    private val SERVICE_UUID: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    private val CHAR_RX: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    private val CHAR_TX: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
    private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    @Volatile
    var connected = false
        private set

    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private var appContext: Context? = null
    private var scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var running = false

    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.i(TAG, "GATT connected, requesting MTU")
                    g.requestMtu(185)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.i(TAG, "GATT disconnected")
                    connected = false
                    rxChar = null
                    g.close()
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            Log.i(TAG, "MTU=$mtu status=$status")
            g.discoverServices()
        }

        @Suppress("DEPRECATION")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "service discovery failed: $status")
                g.disconnect()
                return
            }
            val svc = g.getService(SERVICE_UUID)
            rxChar = svc?.getCharacteristic(CHAR_RX)
            val tx = svc?.getCharacteristic(CHAR_TX)
            if (rxChar == null || tx == null) {
                Log.w(TAG, "NUS characteristics not found")
                g.disconnect()
                return
            }
            g.setCharacteristicNotification(tx, true)
            val desc = tx.getDescriptor(CCCD)
            desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            g.writeDescriptor(desc)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                connected = true
                Log.i(TAG, "ready (notifications enabled)")
            } else {
                Log.w(TAG, "CCCD write failed: $status")
                g.disconnect()
            }
        }
    }

    fun start(context: Context) {
        if (running) return
        running = true
        appContext = context.applicationContext
        scope.launch { connectLoop() }
    }

    fun stop() {
        running = false
        scope.coroutineContext.cancelChildren()
        disconnectGatt()
        connected = false
    }

    /** Writes one protocol line (should already end with '\n'). */
    @Suppress("DEPRECATION")
    fun send(line: String): Boolean {
        val g = gatt ?: return false
        val c = rxChar ?: return false
        if (!connected) return false
        c.value = line.toByteArray(Charsets.UTF_8)
        return try {
            g.writeCharacteristic(c)
        } catch (e: Exception) {
            Log.w(TAG, "write failed: ${e.message}")
            false
        }
    }

    // ------------------------------------------------------------------ //

    private suspend fun connectLoop() {
        val context = appContext ?: return
        val bm = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager?
        val adapter = bm?.adapter

        while (running && adapter != null) {
            try {
                if (!adapter.isEnabled) {
                    delay(3000)
                    continue
                }
                val device = findDevice(adapter)
                if (device == null) {
                    delay(2000)
                    continue
                }
                Log.i(TAG, "connecting to ${device.address}")
                connected = false
                gatt = device.connectGatt(
                    context, false, gattCallback, BluetoothDevice.TRANSPORT_LE
                )

                var waited = 0
                while (running && !connected && waited < 12000) {
                    delay(250)
                    waited += 250
                }
                if (!connected) {
                    Log.w(TAG, "connect timeout, retrying")
                    disconnectGatt()
                    delay(2000)
                    continue
                }

                while (running && connected) {
                    delay(500)
                }
                delay(2000)  // settle before reconnecting
            } catch (e: Exception) {
                Log.w(TAG, "connect loop: ${e.message}")
                delay(2000)
            }
        }
    }

    @SuppressLint("MissingPermission")
    private suspend fun findDevice(adapter: BluetoothAdapter): BluetoothDevice? =
        withTimeoutOrNull(6000) {
            suspendCancellableCoroutine { cont ->
                val scanner = adapter.bluetoothLeScanner
                if (scanner == null) {
                    if (cont.isActive) cont.resume(null)
                    return@suspendCancellableCoroutine
                }
                val filter = ScanFilter.Builder().setDeviceName(DEVICE_NAME).build()
                val settings = ScanSettings.Builder()
                    .setScanMode(ScanSettings.SCAN_MODE_BALANCED)
                    .build()
                val cb = object : ScanCallback() {
                    override fun onScanResult(callbackType: Int, result: ScanResult) {
                        try { scanner.stopScan(this) } catch (_: Exception) {}
                        if (cont.isActive) cont.resume(result.device)
                    }

                    override fun onScanFailed(errorCode: Int) {
                        Log.w(TAG, "scan failed: $errorCode")
                        if (cont.isActive) cont.resume(null)
                    }
                }
                scanner.startScan(listOf(filter), settings, cb)
                cont.invokeOnCancellation {
                    try { scanner.stopScan(cb) } catch (_: Exception) {}
                }
            }
        }

    private fun disconnectGatt() {
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (_: Exception) {
        }
        gatt = null
    }
}
