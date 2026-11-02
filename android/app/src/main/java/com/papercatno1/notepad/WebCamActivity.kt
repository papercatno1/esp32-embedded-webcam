package com.papercatno1.notepad

import android.annotation.SuppressLint
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Toast
import androidx.activity.ComponentActivity

/**
 * 照相模式：把设备的 8080 端口页面（MJPEG 流）塞进 WebView。
 * 地址从设备 SYS 回执里的 ip= 取，取不到就用上次记住的。
 */
class WebCamActivity : ComponentActivity() {

    private lateinit var web: WebView
    private lateinit var ble: BleLink
    private var loaded = false

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        web = WebView(this)
        web.settings.javaScriptEnabled = true
        web.webViewClient = WebViewClient()
        setContentView(web)

        val prefs = getSharedPreferences("cam", MODE_PRIVATE)
        val lastIp = prefs.getString("ip", null)
        if (lastIp != null) loadCam(lastIp)          // 先用上次的地址打开
        else web.loadData("<h3 style='font-family:sans-serif'>正在连接设备…</h3>", "text/html", "utf-8")

        ble = BleLink(this) { msg -> runOnUiThread { onBle(msg, prefs) } }
        ensurePerms()
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

    private fun onBle(msg: String, prefs: android.content.SharedPreferences) {
        val i = msg.indexOf("OK SYS ")
        if (i < 0) return
        val ip = msg.substring(i + 7).split(" ")
            .firstOrNull { it.startsWith("ip=") }?.removePrefix("ip=")
        if (ip.isNullOrBlank()) return
        prefs.edit().putString("ip", ip).apply()
        if (!loaded) {
            loaded = true
            ble.send("MODE cam")          // 让设备真的进照相模式
            loadCam(ip)
        }
    }

    private fun loadCam(ip: String) {
        web.loadUrl("http://$ip:8080/")
        Toast.makeText(this, "设备 $ip:8080", Toast.LENGTH_SHORT).show()
    }

    override fun onDestroy() {
        if (this::ble.isInitialized) ble.close()
        super.onDestroy()
    }
}
