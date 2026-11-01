package com.papercatno1.notepad

import android.content.Intent
import android.graphics.Bitmap
import android.os.Build
import android.os.Bundle
import android.util.Base64
import android.widget.Button
import android.widget.ImageView
import android.widget.SeekBar
import androidx.activity.ComponentActivity

/**
 * 第二步：所见即所得。
 * 直接把「二值化之后、发到屏幕上会变成什么样」画出来（放大 3 倍显示），
 * 拖动浓淡滑块会即时重画——用户看着效果决定参数，而不是盲调。
 */
class PreviewActivity : ComponentActivity() {

    private var small: Bitmap? = null
    private lateinit var image: ImageView
    private lateinit var seek: SeekBar
    private var lastBits: ByteArray? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_preview)

        small = if (Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableExtra("bmp", Bitmap::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra("bmp")
        }
        if (small == null) {
            setResult(RESULT_CANCELED)
            finish()
            return
        }

        image = findViewById(R.id.previewImage)
        seek = findViewById(R.id.seekBias)
        seek.max = 60
        seek.progress = 12

        val zoom = 3                            // 320x212 -> 960x636
        fun render() {
            val b = small ?: return
            val bits = ImgCodec.toBits(b, seek.progress)
            lastBits = bits
            val one = ImgCodec.bitsToBitmap(bits, b.width, b.height)
            image.setImageBitmap(
                Bitmap.createScaledBitmap(one, b.width * zoom, b.height * zoom, false)
            )
        }

        seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) = render()
            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })
        render()

        findViewById<Button>(R.id.btnPreviewBack).setOnClickListener {
            setResult(RESULT_CANCELED)
            finish()
        }

        findViewById<Button>(R.id.btnPreviewSend).setOnClickListener {
            val b = small ?: return@setOnClickListener
            val bits = lastBits ?: ImgCodec.toBits(b, seek.progress)
            val rle = ImgCodec.rleEncode(bits)
            val out = Intent()
            out.putExtra("count", 1)
            out.putExtra("b64_0", Base64.encodeToString(rle, Base64.NO_WRAP))
            out.putExtra("len_0", rle.size)
            setResult(RESULT_OK, out)
            finish()
        }
    }
}
