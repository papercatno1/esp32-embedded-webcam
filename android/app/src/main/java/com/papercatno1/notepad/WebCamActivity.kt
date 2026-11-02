package com.papercatno1.notepad

import android.annotation.SuppressLint
import android.app.AlertDialog
import android.app.DownloadManager
import android.net.Uri
import android.os.Environment
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.content.ContentValues
import android.provider.MediaStore
import android.util.Base64
import android.webkit.JavascriptInterface
import android.webkit.JsResult
import android.webkit.WebChromeClient
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import androidx.activity.ComponentActivity

/**
 * 照相模式：把设备 8080 端口的页面塞进 WebView。
 * 连上 BLE 后每隔 1.2 秒发一次 SYS 直到拿到 ip=，再切进视频页。
 */
private class Saver(private val ctx: Context) {
    @JavascriptInterface
    fun save(name: String, b64: String): String {
        return try {
            val bytes = Base64.decode(b64, Base64.DEFAULT)
            val safe = name.replace(Regex("[^A-Za-z0-9._-]"), "_")
            if (android.os.Build.VERSION.SDK_INT >= 29) {
                val cv = ContentValues().apply {
                    put(MediaStore.Downloads.DISPLAY_NAME, safe)
                    put(MediaStore.Downloads.MIME_TYPE, "image/jpeg")
                    put(MediaStore.Downloads.IS_PENDING, 1)
                }
                val res = ctx.contentResolver
                val uri = res.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)
                    ?: return "存盘失败：无法创建文件"
                res.openOutputStream(uri)?.use { it.write(bytes) } ?: return "存盘失败：打不开输出流"
                cv.clear(); cv.put(MediaStore.Downloads.IS_PENDING, 0)
                res.update(uri, cv, null, null)
            } else {
                val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
                if (!dir.exists()) dir.mkdirs()
                java.io.File(dir, safe).writeBytes(bytes)
            }
            "已保存到「下载」：" + safe + "（" + bytes.size + " 字节）"
        } catch (e: Exception) {
            "存盘失败：" + e.message
        }
    }
}

class WebCamActivity : ComponentActivity() {
    private var s_retry = 0
    private fun logD(m: String) { android.util.Log.i("WebCam", m) }

    /** 先探测 8080 是否起来了，起来了再 loadUrl，避免闪一次"网页无法打开" */
    private fun probeThenLoad(web: WebView, ip: String, n: Int = 0) {
        if (n == 0) {
            web.loadDataWithBaseURL(
                null,
                "<html><body style='margin:0;background:#0e1114;color:#8b949e;" +
                    "font-family:sans-serif;display:flex;align-items:center;" +
                    "justify-content:center;height:100vh;font-size:15px'>正在连接设备…</body></html>",
                "text/html", "utf-8", null
            )
        }
        Thread {
            val ok = try {
                val c = java.net.URL("http://$ip:8080/status.json").openConnection()
                    as java.net.HttpURLConnection
                c.connectTimeout = 1200
                c.readTimeout = 1200
                val code = c.responseCode
                c.disconnect()
                code in 200..299
            } catch (e: Exception) { false }
            runOnUiThread {
                if (ok) {
                    logD("设备网页已就绪，加载")
                    s_retry = 0
                    web.loadUrl("http://$ip:8080/?t=" + System.currentTimeMillis())
                } else if (n < 20) {
                    logD("设备网页未就绪 ${n + 1}/20")
                    web.postDelayed({ probeThenLoad(web, ip, n + 1) }, 800)
                } else {
                    logD("20 次探测都失败：确认设备已进入照相模式、手机与板子在同一 WiFi")
                }
            }
        }.start()
    }



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
        // 每次都取设备上的最新页面，避免 WebView 缓存让人看不到新功能
        web.settings.cacheMode = WebSettings.LOAD_NO_CACHE
        web.addJavascriptInterface(Saver(this), "ESP32Saver")
        web.settings.domStorageEnabled = true
        web.clearCache(true)

        // 页面里 confirm() 需要 WebChromeClient 才会弹窗，否则点「删除」没反应
        web.webChromeClient = object : WebChromeClient() {
            override fun onJsConfirm(
                view: WebView?, url: String?, message: String?, result: JsResult?
            ): Boolean {
                if (result == null) return false
                AlertDialog.Builder(this@WebCamActivity)
                    .setMessage(message)
                    .setPositiveButton("确定") { _, _ -> result.confirm() }
                    .setNegativeButton("取消") { _, _ -> result.cancel() }
                    .setOnCancelListener { result.cancel() }
                    .show()
                return true
            }
        }

        // 卡内照片「下载」走 Content-Disposition 附件，交给系统下载管理器保存到「下载」
        web.setDownloadListener { url, _, contentDisposition, _, _ ->
            try {
                val name = contentDisposition
                    ?.substringAfter("filename=\"", "")
                    ?.substringBefore("\"")
                    ?.takeIf { it.isNotEmpty() }
                    ?: ("esp32cam_" + System.currentTimeMillis() + ".jpg")
                val req = DownloadManager.Request(Uri.parse(url))
                    .setTitle(name)
                    .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                    .setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, name)
                (getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager).enqueue(req)
                runOnUiThread { logD("开始下载 " + name + "（通知栏可见进度）") }
            } catch (e: Exception) {
                runOnUiThread { logD("下载失败：" + e.message) }
            }
        }
        web.webViewClient = object : WebViewClient() {
            override fun onReceivedError(v: WebView, req: WebResourceRequest, err: WebResourceError) {
                if (!req.isForMainFrame) return
                if (s_retry >= 3) { logD("网页重试 3 次仍失败，请确认设备已进入照相模式"); return }
                s_retry++
                logD("加载失败，${s_retry}/3 次重试…")
                v.postDelayed({ v.reload() }, 1500)
            }
        }
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
        probeThenLoad(web, ip)
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
