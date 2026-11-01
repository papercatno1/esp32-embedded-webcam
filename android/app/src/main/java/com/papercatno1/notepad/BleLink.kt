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
import android.bluetooth.BluetoothStatusCodes
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
 * 关键点：BLE 的「有回执写」(WRITE_TYPE_DEFAULT) **同一时刻只能有一个在飞**，
 * 上一条没等到 onCharacteristicWrite 就再写，Android 会直接返回 false，
 * 表现就是「点了按钮、日志显示发出了、设备却没反应」。
 * 所以这里用「队列 + inFlight 标记 + 3 秒看门狗」串行化所有写操作。
 */
class BleLink(context: Context, private val onEvent: (String) -> Unit) {

    companion object {
        private val SVC: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        private val RX: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        private val TX: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val DEVICE_NAME = "ESP32-NOTE"
        private const val WRITE_TIMEOUT_MS = 3000L
    }

    private val handler = Handler(Looper.getMainLooper())
    private val appContext = context.applicationContext

    private var adapter: BluetoothAdapter? = null
    private var gatt: BluetoothGatt? = null
    private var lastDevice: BluetoothDevice? = null

    private var rxChar: BluetoothGattCharacteristic? = null
    private var txChar: BluetoothGattCharacteristic? = null

    var mtu: Int = 23
        private set
    var connected: Boolean = false
        private set
    /** 服务和特征都拿到、可以真正发指令 */
    var ready: Boolean = false
        private set
    /** 回执通道（notify）是否订阅成功 */
    var notifyReady: Boolean = false
        private set

    private val queue = ArrayDeque<ByteArray>()
    private var inFlight = false

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
        lastDevice = device
        closeGatt()
        log("连接 ${device.address} ...")
        gatt = device.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    /** 手动重连上一次的设备（按钮用） */
    @SuppressLint("MissingPermission")
    fun reconnect() {
        val d = lastDevice
        if (d == null) {
            log("还没连过设备，先点「扫描连接」")
            return
        }
        connect(d)
    }

    private val gattCallback = object : BluetoothGattCallback() {

        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                connected = true
                log("已连接，发现服务...")
                g.discoverServices()
            } else {
                connected = false
                ready = false
                notifyReady = false
                rxChar = null
                txChar = null
                mtu = 23
                inFlight = false
                handler.removeCallbacks(writeTimeout)
                queue.clear()
                log("断开 (status=$status)")
                g.close()
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                log("发现服务失败 status=$status，重试一次")
                g.discoverServices()
                return
            }
            val svc = g.getService(SVC)
            if (svc == null) {
                log("找不到 NUS 服务（共有 ${g.services?.size ?: 0} 个服务）")
                g.disconnect()
                return
            }
            rxChar = svc.getCharacteristic(RX)
            txChar = svc.getCharacteristic(TX)
            if (rxChar == null || txChar == null) {
                log("NUS 特征缺失 rx=${rxChar != null} tx=${txChar != null}")
                g.disconnect()
                return
            }
            ready = true
            log("服务就绪，协商 MTU...")
            if (!g.requestMtu(517)) {
                log("MTU 协商请求被拒，用 23 字节分片")
                enableNotify(g)
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            mtu = newMtu
            log("MTU=$newMtu (status=$status)")
            enableNotify(g)
        }

        /** 订阅回执通道；失败会隔 600ms 再试一次 */
        @SuppressLint("MissingPermission")
        private fun enableNotify(g: BluetoothGatt, retry: Boolean = true) {
            val tx = txChar ?: return
            val ok = g.setCharacteristicNotification(tx, true)
            val desc = tx.getDescriptor(CCCD)
            if (desc == null) {
                log("找不到 CCCD 描述符，收不到回执")
                return
            }
            val wrote = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                g.writeDescriptor(desc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) ==
                    BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                g.writeDescriptor(desc)
            }
            if (!wrote && retry) {
                log("订阅回执写入被拒，600ms 后重试")
                handler.postDelayed({ if (ready) enableNotify(g, false) }, 600)
            } else if (!wrote) {
                log("订阅回执失败：能发指令，但看不到设备回执")
            }
        }

        @Suppress("DEPRECATION")
        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                notifyReady = true
                log("回执订阅成功")
            } else {
                log("回执订阅失败 status=$status（重连一次试试）")
            }
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
            val text = ch.value?.toString(Charsets.UTF_8) ?: ""
            if (text.isNotBlank()) log("<- ${text.trim()}")
        }

        // Android 13+ 的新回调（旧回调仍会被调用，这里只兜底）
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            ch: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
            val text = value.toString(Charsets.UTF_8)
            if (text.isNotBlank()) log("<- ${text.trim()}")
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            ch: BluetoothGattCharacteristic,
            status: Int
        ) {
            handler.removeCallbacks(writeTimeout)
            inFlight = false
            if (status != BluetoothGatt.GATT_SUCCESS) log("写回执 status=$status")
            drainQueue()
        }
    }

    private val writeTimeout = Runnable {
        inFlight = false
        log("写超时 3 秒，跳过这一片（剩 ${queue.size}）")
        drainQueue()
    }

    // ---------------- 发送 ----------------
    /** 发送一行指令（自动补 '\n'、按 MTU 切片、排队串行发） */
    fun send(command: String) {
        if (!ready) {
            log("服务还没就绪（connected=$connected），这条没发：$command")
            return
        }
        val payload = if (command.endsWith("\n")) command else "$command\n"
        val bytes = payload.toByteArray(Charsets.UTF_8)
        log("-> ${command.take(60)}${if (command.length > 60) "…" else ""}  (${bytes.size} 字节)")
        val chunk = maxOf(20, mtu - 3)
        var off = 0
        while (off < bytes.size) {
            val end = minOf(off + chunk, bytes.size)
            queue.addLast(bytes.copyOfRange(off, end))
            off = end
        }
        drainQueue()
    }

    private fun drainQueue() {
        if (inFlight) return
        val g = gatt
        val ch = rxChar
        if (g == null || ch == null) {
            if (queue.isNotEmpty()) {
                log("链路未就绪（gatt=${g != null} rx=${ch != null}），${queue.size} 片丢弃")
                queue.clear()
            }
            return
        }
        val next = queue.removeFirstOrNull() ?: return
        inFlight = true
        handler.postDelayed(writeTimeout, WRITE_TIMEOUT_MS)
        if (!writeChunk(g, ch, next)) {
            handler.removeCallbacks(writeTimeout)
            inFlight = false
            // GATT 正忙：等一会儿再试，不要丢数据
            if (queue.size > 64) {
                log("GATT 长时间忙，清掉 ${queue.size} 片")
                queue.clear()
            } else {
                handler.postDelayed({ drainQueue() }, 150)
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun writeChunk(
        g: BluetoothGatt,
        ch: BluetoothGattCharacteristic,
        bytes: ByteArray
    ): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(
                ch, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            ) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            ch.value = bytes
            @Suppress("DEPRECATION")
            g.writeCharacteristic(ch)
        }
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt() {
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (_: Exception) {
        }
        gatt = null
        rxChar = null
        txChar = null
        connected = false
        ready = false
        notifyReady = false
        mtu = 23
        inFlight = false
        queue.clear()
        handler.removeCallbacks(writeTimeout)
    }

    @SuppressLint("MissingPermission")
    fun close() = closeGatt()

    private fun log(msg: String) = onEvent(msg)
}
