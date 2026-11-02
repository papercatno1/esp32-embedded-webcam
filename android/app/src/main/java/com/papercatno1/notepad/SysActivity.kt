package com.papercatno1.notepad

import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.ScrollView
import android.widget.Toast
import androidx.activity.ComponentActivity

/** 原生系统：连上设备读 SYS，显示系统信息；可把设备切到系统页。 */
class SysActivity : ComponentActivity() {

    private lateinit var ble: BleLink
    private lateinit var tvStatus: TextView
    private lateinit var tvInfo: TextView
    private lateinit var tvLog: TextView
    private lateinit var logWrap: ScrollView

    private val permReq = 1
    private var invState = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_sys)

        tvStatus = findViewById(R.id.tvStatus)
        tvInfo = findViewById(R.id.tvInfo)
        tvLog = findViewById(R.id.tvLog)
        logWrap = findViewById(R.id.logWrap)

        ble = BleLink(this) { msg -> runOnUiThread { onBle(msg) } }

        findViewById<Button>(R.id.btnConnect).setOnClickListener { ensurePerms() }
        findViewById<Button>(R.id.btnRefresh).setOnClickListener { askSys() }
        // 屏幕白底黑字就是反显状态，点一下切过去看哪个对
        findViewById<Button>(R.id.btnInv).setOnClickListener {
            if (!ble.connected) { toast("还没连上设备，先点「扫描连接」"); return@setOnClickListener }
            invState = 1 - invState
            ble.send("INV $invState")
            toast(if (invState == 1) "已发 INV 1（打开反显）" else "已发 INV 0（关闭反显）")
        }

        findViewById<Button>(R.id.btnSysScreen).setOnClickListener {
            if (!ble.connected) { toast("还没连上设备，先点「扫描连接」"); return@setOnClickListener }
            ble.send("MODE sys")
            toast("已发指令：切到系统页")
        }
        ensurePerms()
    }

    private fun ensurePerms() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val need = arrayOf(
                android.Manifest.permission.BLUETOOTH_SCAN,
                android.Manifest.permission.BLUETOOTH_CONNECT
            ).filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
            if (need.isNotEmpty()) { requestPermissions(need.toTypedArray(), permReq); return }
        }
        ble.startScan()
    }

    override fun onRequestPermissionsResult(code: Int, p: Array<String>, r: IntArray) {
        super.onRequestPermissionsResult(code, p, r)
        if (code == permReq) ble.startScan()
    }

    private fun askSys() {
        if (!ble.connected) { toast("还没连上设备，先点「扫描连接」"); return }
        ble.send("SYS")
    }

    private var autoAsked = false

    private fun onBle(msg: String) {
        appendLog(msg)
        tvStatus.text = when {
            ble.ready -> "已连接 · MTU ${ble.mtu}"
            ble.connected -> "已连上，正在初始化通道…"
            else -> "未连接（点“扫描连接”）"
        }
        // 通道一就绪就自动读一次，省得用户还要点
        if (ble.ready && !autoAsked) { autoAsked = true; ble.send("SYS") }
        val i = msg.indexOf("OK SYS ")
        if (i >= 0) tvInfo.text = prettySys(msg.substring(i + 7))
    }

    /** "ver=0.4.0 psram=7MB ..." -> 一行一项，方便看 */
    private fun prettySys(s: String): String {
        val names = mapOf(
            "ver" to "固件版本", "mode" to "当前模式", "psram" to "PSRAM",
            "flash" to "FLASH", "tf" to "TF 卡", "bat" to "电池",
            "temp" to "芯片温度", "heap" to "可用内存", "wifi" to "WiFi",
            "ble" to "蓝牙", "up" to "已运行(秒)", "notes" to "消息条数"
        )
        val sb = StringBuilder()
        for (kv in s.trim().split(" ")) {
            val p = kv.split("=", limit = 2)
            if (p.size == 2) {
                val k = names[p[0]] ?: p[0]
                val v = when (p[0]) {
                    "wifi" -> if (p[1] == "1") "已连" else "未连"
                    "ble" -> if (p[1] == "1") "已连" else "广播中"
                    "mode" -> when (p[1]) { "sys" -> "原生系统"; "cam" -> "照相"; else -> "票据屏" }
                    else -> p[1]
                }
                sb.append(k).append("：").append(v).append("\n")
            }
        }
        return sb.toString().trimEnd()
    }

    private fun appendLog(line: String) {
        tvLog.append(line + "\n")
        logWrap.post { logWrap.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_SHORT).show()

    override fun onDestroy() {
        ble.close()
        super.onDestroy()
    }
}
