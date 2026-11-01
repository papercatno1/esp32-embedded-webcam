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
        info.text = "拖动/双指缩放取景；绿框里就是屏幕上显示的样子"

        findViewById<Button>(R.id.btnCropCancel).setOnClickListener {
            setResult(RESULT_CANCELED)
            finish()
        }

        val biasSeek = findViewById<android.widget.SeekBar>(R.id.seekBias)
        val pageBtn = findViewById<Button>(R.id.btnCropPages)
        biasSeek.progress = 10
        val pageList = intArrayOf(1, 2, 4)
        var pageIdx = 0
        pageBtn.setOnClickListener {
            pageIdx = (pageIdx + 1) % pageList.size
            val n = pageList[pageIdx]
            view.setPages(n)
            pageBtn.text = "页数：$n"
            info.text = when (n) {
                1 -> "一页：整块信息一屏放下，小字可能糊"
                2 -> "两页：框变高，字放大 2 倍，翻一下看下一页"
                else -> "四页：字放大 4 倍，最清楚；框会更细长"
            }
        }

        findViewById<Button>(R.id.btnCropOk).setOnClickListener {
            val cut = view.cropToScreen()
            if (cut == null) {
                Toast.makeText(this, "取景框没准备好", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val bias = biasSeek.progress
            val nPage = maxOf(1, cut.height / 212)
            val tiles = ArrayList<Bitmap>()
            for (k in 0 until nPage) tiles.add(Bitmap.createBitmap(cut, 0, k * 212, 320, 212))
            val out = Intent()
            out.putExtra("count", tiles.size)
            tiles.forEachIndexed { i, pg ->
                val rle = ImgCodec.rleEncode(ImgCodec.toBits(pg, bias))
                out.putExtra("b64_$i", Base64.encodeToString(rle, Base64.NO_WRAP))
                out.putExtra("len_$i", rle.size)
            }
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
