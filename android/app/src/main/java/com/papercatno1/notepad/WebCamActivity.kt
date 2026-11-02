package com.papercatno1.notepad

import android.annotation.SuppressLint
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.ComponentActivity

/**
 * 照相模式：把设备 8080 端口的页面塞进 WebView。
 * 连上 BLE 后每隔 1.2 秒发一次 SYS 直到拿到 ip=，再切进视频页。
 */
class WebCamActivity : ComponentActivity() {

    private lateinit var web: WebView
    private lateinit var ble: BleLink
    private lateinit var prefs: SharedPreferences
    private val handler = Handler(Looper.getMainLooper())
    private var done = false
    private var tries = 0

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = getSharedPreferences("cam", Context.MODE_PRIVATE)

        web = WebView(this)
        web.settings.javaScriptEnabled = true
        web.webViewClient = WebViewClient()
        setContentView(web)

        ble = BleLink(this) { msg -> runOnUiThread { onBle(msg) } }
        showStatus("正在连接设备…", "扫描 ESP32-NOTE")
        ensurePerms()
        handler.postDelayed(poll, 800)
    }

    private val poll = object : Runnable {
        override fun run() {
            if (done) return
            if (ble.ready) {
                tries++
                ble.send("SYS")
                showStatus("正在读取设备地址…", "第 $tries 次询问")
                if (tries < 15) handler.postDelayed(this, 1200)
                else showStatus("没读到设备地址", "确认设备已开机；可退回菜单重进一次")
            } else {
                showStatus(
                    if (ble.connected) "已连上，正在初始化通道…" else "正在连接设备…",
                    "扫描 ESP32-NOTE"
                )
                handler.postDelayed(this, 1200)
            }
        }
    }

    private fun ensurePerms() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val need = arrayOf(
                android.Manifest.permission.BLUETOOTH_SCAN,
                android.Manifest.permission.BLUETOOTH_CONNECT
            ).filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
            if (need.isNotEmpty()) { requestPermissions(need.toTypedArray(), 1); return }
        }
        ble.startScan()
    }

    override fun onRequestPermissionsResult(code: Int, p: Array<String>, r: IntArray) {
        super.onRequestPermissionsResult(code, p, r)
        if (code == 1) ble.startScan()
    }

    private fun onBle(msg: String) {
        if (done) return
        val i = msg.indexOf("OK SYS ")
        if (i < 0) return
        val ip = msg.substring(i + 7).split(" ")
            .firstOrNull { it.startsWith("ip=") }?.removePrefix("ip=")
        if (ip.isNullOrBlank()) return
        done = true
        handler.removeCallbacks(poll)
        prefs.edit().putString("ip", ip).apply()
        ble.send("MODE cam")                 // 让设备真的进照相模式
        web.loadUrl("http://$ip:8080/")
    }

    private fun showStatus(title: String, sub: String) {
        web.loadDataWithBaseURL(
            null,
            "<body style='margin:0;background:#111;color:#eee;font-family:sans-serif;" +
                "display:flex;flex-direction:column;justify-content:center;height:100vh;text-align:center'>" +
                "<div style='font-size:18px'>" + title + "</div>" +
                "<div style='font-size:13px;color:#8a8;margin-top:8px'>" + sub + "</div></body>",
            "text/html", "utf-8", null
        )
    }

    override fun onDestroy() {
        handler.removeCallbacks(poll)
        if (this::ble.isInitialized) ble.close()
        super.onDestroy()
    }
}
