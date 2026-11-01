package com.papercatno1.notepad

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Bundle
import android.util.Base64
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts

/**
 * 第一步：框选。只负责“选哪一块”，不碰浓淡。
 * 点“下一步”把 320x212 的原色小块交给 PreviewActivity。
 */
class CropActivity : ComponentActivity() {

    private lateinit var view: CropView

    private val preview = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { res ->
        if (res.resultCode == RESULT_OK && res.data != null) {
            setResult(RESULT_OK, res.data)      // 原样转交 MainActivity
        } else {
            setResult(RESULT_CANCELED)
        }
        finish()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_crop)

        view = findViewById(R.id.cropView)
        val info = findViewById<TextView>(R.id.cropInfo)
        val bmp = loadBitmap(intent.data)
        if (bmp == null) {
            Toast.makeText(this, "这张图读不出来", Toast.LENGTH_SHORT).show()
            setResult(RESULT_CANCELED)
            finish()
            return
        }
        view.setBitmap(bmp)
        info.text = "拖动图片、双指缩放；绿框里就是屏幕上会显示的区域"

        findViewById<Button>(R.id.btnCropCancel).setOnClickListener {
            setResult(RESULT_CANCELED)
            finish()
        }

        findViewById<Button>(R.id.btnCropOk).setOnClickListener {
            val small = view.cropToScreen()
            if (small == null) {
                Toast.makeText(this, "取景框没准备好", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val it = Intent(this, PreviewActivity::class.java)
            it.putExtra("bmp", small)
            preview.launch(it)
        }
    }

    /** 长截图动辄 3000+ px，先按 2 的幂降采样到 2000 px 以内，避免 OOM */
    private fun loadBitmap(uri: Uri?): Bitmap? {
        if (uri == null) return null
        return try {
            val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
            var sample = 1
            while (maxOf(opts.outWidth, opts.outHeight) / sample > 2000) sample *= 2
            val real = BitmapFactory.Options().apply { inSampleSize = sample }
            contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, real) }
        } catch (e: Exception) {
            null
        }
    }
}
