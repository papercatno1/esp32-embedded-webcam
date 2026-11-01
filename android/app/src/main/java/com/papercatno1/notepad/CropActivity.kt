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

/** 选好图后进来：拖动/缩放取景框，框内就是发到屏幕上的样子 */
class CropActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_crop)

        val view = findViewById<CropView>(R.id.cropView)
        val info = findViewById<TextView>(R.id.cropInfo)
        val bmp = loadBitmap(intent.data)
        if (bmp == null) {
            Toast.makeText(this, "这张图读不出来", Toast.LENGTH_SHORT).show()
            setResult(RESULT_CANCELED)
            finish()
            return
        }
        view.setBitmap(bmp)
        info.text = "拖动图片、双指缩放；绿框里就是屏幕上显示的样子"

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
            val bits = ImgCodec.toBits(small)
            val rle = ImgCodec.rleEncode(bits)
            val b64 = Base64.encodeToString(rle, Base64.NO_WRAP)
            val out = Intent()
            out.putExtra("w", small.width)
            out.putExtra("h", small.height)
            out.putExtra("rle", rle.size)
            out.putExtra("b64", b64)
            setResult(RESULT_OK, out)
            finish()
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
