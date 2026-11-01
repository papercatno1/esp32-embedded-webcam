package com.papercatno1.notepad

import android.Manifest
import android.app.Activity
import android.content.ClipboardManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

class MainActivity : Activity() {

    private lateinit var ble: BleLink
    private lateinit var statusView: TextView
    private lateinit var logView: TextView
    private lateinit var logWrap: ScrollView
    private lateinit var input: EditText
    private var lastReply = ""

    private val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT
        )
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        statusView = findViewById(R.id.status)
        logView = findViewById(R.id.log)
        logWrap = findViewById(R.id.logWrap)
        input = findViewById(R.id.input)

        ble = BleLink(this) { msg -> runOnUiThread { appendLog(msg) } }

        findViewById<Button>(R.id.btnScan).setOnClickListener {
            if (!hasPermissions()) {
                requestPermissions(permissions, 1001)
            } else {
                ble.startScan()
            }
        }

        findViewById<Button>(R.id.btnPaste).setOnClickListener { pasteClipboard() }

        findViewById<Button>(R.id.btnSend).setOnClickListener { sendNote() }

        findViewById<Button>(R.id.btnVer).setOnClickListener { send("V") }

        findViewById<Button>(R.id.btnList).setOnClickListener { send("L") }

        findViewById<Button>(R.id.btnClearDev).setOnClickListener { send("C") }

        findViewById<Button>(R.id.btnPrev).setOnClickListener { send("R") }

        findViewById<Button>(R.id.btnNext).setOnClickListener { send("N") }

        findViewById<Button>(R.id.btnAlwaysOn).setOnClickListener { send("O 0") }

        findViewById<Button>(R.id.btnIdle60).setOnClickListener { send("O 60") }

        findViewById<Button>(R.id.btnAccel).setOnClickListener { send("A") }

        findViewById<Button>(R.id.btnSwipeStat).setOnClickListener { send("Q") }

        // 甩动翻页方向反了就点这个：K t 让设备自己取反并保存到 NVS
        findViewById<Button>(R.id.btnFlipSign).setOnClickListener { send("K t") }

        updateStatus()
    }

    private fun hasPermissions(): Boolean =
        permissions.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 1001 && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            ble.startScan()
        } else if (requestCode == 1001) {
            toast("需要蓝牙权限才能连接设备")
        }
    }

    private fun pasteClipboard() {
        val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val text = cm.primaryClip?.getItemAt(0)?.coerceToText(this)?.toString()
        if (text.isNullOrBlank()) {
            toast("剪贴板是空的")
            return
        }
        input.setText(text.trim())
        toast("已粘贴 ${text.length} 字")
    }

    /**
     * 第一行当标题，其余行当正文，用 '|' 拼成设备端的 T 指令。
     */
    private fun sendNote() {
        val raw = input.text.toString().trim()
        if (raw.isEmpty()) {
            toast("先写点内容")
            return
        }
        if (!ble.ready) {
            toast(if (ble.connected) "还在等服务就绪，稍等下" else "还没连上设备")
            return
        }
        val lines = raw.split("\n").map { it.trim() }.filter { it.isNotEmpty() }
        if (lines.isEmpty()) {
            toast("内容为空")
            return
        }
        val payload = "T " + lines.joinToString("|")
        if (payload.toByteArray(Charsets.UTF_8).size > 1000) {
            toast("内容太长，拆成两张吧")
            return
        }
        ble.send(payload)
    }

    private fun send(cmd: String) {
        if (!ble.ready) {
            toast(if (ble.connected) "还在等服务就绪，稍等下" else "还没连上设备")
            return
        }
        ble.send(cmd)
    }

    private fun appendLog(msg: String) {
        logView.append(msg + "\n")
        lastReply = msg
        updateStatus()
        logWrap.post { logWrap.fullScroll(android.view.View.FOCUS_DOWN) }
    }

    private fun updateStatus() {
        val link = when {
            ble.ready -> "已连接 · MTU ${ble.mtu}"
            ble.connected -> "已连接，等服务就绪…"
            else -> "未连接 · 点「扫描连接」"
        }
        statusView.text = if (lastReply.isEmpty()) link else "$link\n最近：$lastReply"
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    override fun onDestroy() {
        super.onDestroy()
        ble.close()
    }
}
