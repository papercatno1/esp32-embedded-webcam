package com.papercatno1.notepad

import android.Manifest
import android.app.Activity
import android.content.ClipboardManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.result.contract.ActivityResultContracts
import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

class MainActivity : androidx.activity.ComponentActivity() {

    private lateinit var ble: BleLink
    private lateinit var statusView: TextView
    private lateinit var logView: TextView
    private lateinit var logWrap: ScrollView
    private lateinit var input: EditText
    private var lastReply = ""

    private val permissions: Array<String> = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
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
        findViewById<Button>(R.id.btnReconnect).setOnClickListener { ble.reconnect() }

        findViewById<Button>(R.id.btnImage).setOnClickListener { pickImage.launch("image/*") }

        updateStatus()
    }

    private fun hasPermissions(): Boolean =
        permissions.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
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

    // ---------------- 图片：选图 -> 缩到 320x212 -> 二值 -> RLE -> base64 -> I/A/E ----------------

    private val pickImage = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri == null) return@registerForActivityResult
        val it = Intent(this, CropActivity::class.java)
        it.data = uri
        cropImage.launch(it)
    }

    /** 裁剪界面回传：宽度、高度、RLE 字节数、base64 正文 */
    private val cropImage = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { res ->
        if (res.resultCode != RESULT_OK) {
            appendLog("取消发送图片")
            return@registerForActivityResult
        }
        val d = res.data ?: return@registerForActivityResult
        val n = d.getIntExtra("count", 0)
        if (n <= 0) {
            toast("没拿到裁剪结果")
            return@registerForActivityResult
        }
        for (i in 0 until n) {
            val b64 = d.getStringExtra("b64_$i") ?: continue
            val len = d.getIntExtra("len_$i", 0)
            if (b64.isEmpty()) continue
            sendImageB64(320, 212, len, b64)
        }
    }

    /** 发一张已经裁好、压成 1-bit 的图：I/A.../E */
    private fun sendImageB64(w: Int, h: Int, rleLen: Int, b64: String) {
        if (!ble.ready) {
            toast(if (ble.connected) "还在等服务就绪，稍等下" else "还没连上设备")
            return
        }
        appendLog("图片 ${w}x$h  RLE ${rleLen} B  base64 ${b64.length} 字符")
        val chunk = 380                                  // 4 的倍数，保证逐行 base64 字节对齐
        ble.send("I $w $h")
        var i = 0
        while (i < b64.length) {
            ble.send("A " + b64.substring(i, minOf(i + chunk, b64.length)))
            i += chunk
        }
        ble.send("E")
    }

    /** 把位图转成 1-bit（1=白 0=黑），行按字节对齐、MSB 在前 */
    private fun bitmapToBits(bmp: android.graphics.Bitmap): ByteArray {
        val w = bmp.width
        val h = bmp.height
        val stride = (w + 7) / 8
        val out = ByteArray(stride * h)
        val row = IntArray(w)
        for (y in 0 until h) {
            bmp.getPixels(row, 0, w, 0, y, w, 1)
            val base = y * stride
            for (x in 0 until w) {
                val c = row[x]
                val lum = (299 * ((c shr 16) and 0xFF) + 587 * ((c shr 8) and 0xFF) + 114 * (c and 0xFF)) / 1000
                if (lum >= 128) out[base + (x shr 3)] = (out[base + (x shr 3)].toInt() or (0x80 shr (x and 7))).toByte()
            }
        }
        return out
    }

    /** 1-bit 位图 RLE：成对 [count][value]，value 只能是 0x00 / 0xFF */
    private fun rleEncode(bits: ByteArray): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        var i = 0
        while (i < bits.size) {
            val v = bits[i]
            var run = 1
            while (i + run < bits.size && bits[i + run] == v && run < 255) run++
            out.write(run)
            out.write(v.toInt() and 0xFF)
            i += run
        }
        return out.toByteArray()
    }

    private fun sendImage(uri: android.net.Uri) {
        if (!ble.ready) {
            toast(if (ble.connected) "还在等服务就绪，稍等下" else "还没连上设备")
            return
        }
        val src = try {
            contentResolver.openInputStream(uri)?.use { android.graphics.BitmapFactory.decodeStream(it) }
        } catch (e: Exception) {
            null
        }
        if (src == null) {
            toast("这张图读不出来")
            return
        }
        // 按比例缩到 320x212 以内
        val scale = minOf(320f / src.width, 212f / src.height, 1f)
        val w = (src.width * scale).toInt().coerceAtLeast(1)
        val h = (src.height * scale).toInt().coerceAtLeast(1)
        val small = android.graphics.Bitmap.createScaledBitmap(src, w, h, true)
        val bits = bitmapToBits(small)
        val rle = rleEncode(bits)
        val b64 = android.util.Base64.encodeToString(rle, android.util.Base64.NO_WRAP)

        appendLog("图片 ${w}x$h  位图 ${bits.size} B  RLE ${rle.size} B")

        // 每行 380 个 base64 字符（4 的倍数，保证设备逐行解码时字节对齐）
        val chunk = 380
        ble.send("I $w $h")
        var i = 0
        while (i < b64.length) {
            ble.send("A " + b64.substring(i, minOf(i + chunk, b64.length)))
            i += chunk
        }
        ble.send("E")
    }

    private fun appendLog(msg: String) {
        logView.append(msg + "\n")
        lastReply = msg
        updateStatus()
        logWrap.post { logWrap.fullScroll(android.view.View.FOCUS_DOWN) }
    }

    private fun updateStatus() {
        val link = when {
            ble.ready && ble.notifyReady -> "已连接 · MTU ${ble.mtu} · 回执正常"
            ble.ready -> "已连接 · MTU ${ble.mtu} · 回执未订阅"
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
