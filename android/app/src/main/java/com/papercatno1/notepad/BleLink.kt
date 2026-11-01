package com.papercatno1.notepad

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
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import java.util.UUID

/**
 * 与 ESP32-NOTE 的 BLE 连接（Nordic UART Service）。
 *
 * 设备端特征：
 *   RX（write）  手机 -> 设备
 *   TX（notify） 设备 -> 手机
 * 所有写操作按 (MTU-3) 切片顺序发送，避免长包被截断。
 */
class BleLink(context: Context, private val onEvent: (String) -> Unit) {

    companion object {
        private val SVC: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        private val RX: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        private val TX: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val DEVICE_NAME = "ESP32-NOTE"
    }

    private val handler = Handler(Looper.getMainLooper())
    private val appContext = context.applicationContext

    private var adapter: BluetoothAdapter? = null
    private var gatt: BluetoothGatt? = null

    private var rxChar: BluetoothGattCharacteristic? = null
    private var txChar: BluetoothGattCharacteristic? = null

    var mtu: Int = 23
        private set

    var connected: Boolean = false
        private set

    private val writeQueue = ArrayDeque<ByteArray>()

    // ---------------- 扫描 ----------------
    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.device.name ?: result.scanRecord?.deviceName ?: ""
            log("发现 ${result.device.address}  $name  ${result.rssi} dBm")
            if (name.contains(DEVICE_NAME, ignoreCase = true) ||
                result.scanRecord?.serviceUuids?.contains(ParcelUuid(SVC)) == true
            ) {
                stopScan()
                log("连接 $name ...")
                connect(result.device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            log("扫描失败 code=$errorCode")
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        val mgr = appContext.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        adapter = mgr?.adapter
        val a = adapter
        if (a == null || !a.isEnabled) {
            log("蓝牙未开启")
            return
        }
        val scanner = a.bluetoothLeScanner
        if (scanner == null) {
            log("不支持 BLE 扫描")
            return
        }
        val filters = listOf(
            ScanFilter.Builder().setServiceUuid(ParcelUuid(SVC)).build(),
            ScanFilter.Builder().setDeviceName(DEVICE_NAME).build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        log("扫描中（先连一次会更快出现）...")
        scanner.startScan(filters, settings, scanCallback)
        handler.postDelayed({ stopScan() }, 12_000)
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        adapter?.bluetoothLeScanner?.stopScan(scanCallback)
    }

    // ---------------- 连接 ----------------
    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        close()
        gatt = device.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission", "Deprecated")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                connected = true
                log("已连接，发现服务...")
                g.discoverServices()
            } else {
                connected = false
                log("断开 (status=$status)")
                g.close()
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(SVC)
            if (svc == null) {
                log("找不到 NUS 服务，确认设备固件")
                g.disconnect()
                return
            }
            rxChar = svc.getCharacteristic(RX)
            txChar = svc.getCharacteristic(TX)
            log("服务就绪，协商 MTU...")
            g.requestMtu(517)
        }

        @SuppressLint("MissingPermission", "Deprecated")
        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            mtu = newMtu
            log("MTU=$newMtu")
            val tx = txChar ?: return
            g.setCharacteristicNotification(tx, true)
            val desc = tx.getDescriptor(CCCD)
            if (desc != null) {
                desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                g.writeDescriptor(desc)
            }
            log("已订阅回执")
        }

        @Suppress("Deprecation")
        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
            val text = ch.value?.toString(Charsets.UTF_8) ?: ""
            log("<- $text")
        }

        @Suppress("Deprecation")
        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            ch: BluetoothGattCharacteristic,
            status: Int
        ) {
            drainQueue()
        }
    }

    // ---------------- 发送 ----------------
    /** 发送一行指令（自动补 '\n' 并按 MTU 切片） */
    fun send(command: String) {
        val payload = if (command.endsWith("\n")) command else "$command\n"
        val bytes = payload.toByteArray(Charsets.UTF_8)
        log("-> ${command.take(60)}${if (command.length > 60) "…" else ""}  (${bytes.size} 字节)")
        writeQueue.clear()
        var off = 0
        val chunk = maxOf(20, mtu - 3)
        while (off < bytes.size) {
            val end = minOf(off + chunk, bytes.size)
            writeQueue.addLast(bytes.copyOfRange(off, end))
            off = end
        }
        drainQueue()
    }

    @SuppressLint("MissingPermission", "Deprecated")
    private fun drainQueue() {
        val g = gatt ?: return
        val ch = rxChar ?: return
        val next = writeQueue.removeFirstOrNull() ?: return
        g.setCharacteristicNotification(txChar, true)
        ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        ch.value = next
        if (!g.writeCharacteristic(ch)) {
            log("写失败，链路可能已断")
            writeQueue.clear()
            return
        }
        // 分片之间稍作间隔，避免 GATT 忙时丢包
        if (writeQueue.isNotEmpty()) handler.postDelayed({ drainQueue() }, 25)
    }

    @SuppressLint("MissingPermission")
    fun close() {
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (_: Exception) {
        }
        gatt = null
        rxChar = null
        txChar = null
        connected = false
        mtu = 23
        writeQueue.clear()
    }

    private fun log(msg: String) = onEvent(msg)
}
