package com.papercatno1.notepad

import android.graphics.Bitmap
import java.io.ByteArrayOutputStream

/** 位图 -> 1-bit -> RLE，格式与固件 notes.cpp 保持一致 */
object ImgCodec {

    /**
     * 局部自适应二值化：拿每个像素和它周围一小圈的均值比，比均值暗就判黑。
     * 全局阈值（一刀切 128）会把小字笔画削掉——屏幕截图白底黑字对比度高，
     * 但抗锯齿后的细笔画灰度在 150~200，一刀切就没了。
     * bias 越大判黑越宽松（字更粗）。
     */
    fun toBits(bmp: Bitmap, bias: Int = 10): ByteArray {
        val w = bmp.width
        val h = bmp.height
        val lum = IntArray(w * h)
        val row = IntArray(w)
        for (y in 0 until h) {
            bmp.getPixels(row, 0, w, 0, y, w, 1)
            for (x in 0 until w) {
                val c = row[x]
                lum[y * w + x] =
                    (299 * ((c shr 16) and 0xFF) + 587 * ((c shr 8) and 0xFF) + 114 * (c and 0xFF)) / 1000
            }
        }
        // 积分图，O(1) 求任意窗口均值
        val iw = w + 1
        val sum = LongArray(iw * (h + 1))
        for (y in 0 until h) {
            var rs = 0L
            for (x in 0 until w) {
                rs += lum[y * w + x]
                sum[(y + 1) * iw + (x + 1)] = sum[y * iw + (x + 1)] + rs
            }
        }
        val r = maxOf(4, minOf(w, h) / 12)
        val stride = (w + 7) / 8
        val out = ByteArray(stride * h)
        for (y in 0 until h) {
            val y0 = maxOf(0, y - r)
            val y1 = minOf(h - 1, y + r)
            val base = y * stride
            for (x in 0 until w) {
                val x0 = maxOf(0, x - r)
                val x1 = minOf(w - 1, x + r)
                val area = (x1 - x0 + 1) * (y1 - y0 + 1)
                val s = sum[(y1 + 1) * iw + (x1 + 1)] - sum[y0 * iw + (x1 + 1)] -
                        sum[(y1 + 1) * iw + x0] + sum[y0 * iw + x0]
                val mean = (s / area).toInt()
                if (lum[y * w + x] >= mean - bias) {
                    out[base + (x shr 3)] = (out[base + (x shr 3)].toInt() or (0x80 shr (x and 7))).toByte()
                }
            }
        }
        return out
    }

    /** 1-bit 位图 -> ARGB 位图（预览用，1 像素 = 1 像素） */
    fun bitsToBitmap(bits: ByteArray, w: Int, h: Int): Bitmap {
        val stride = (w + 7) / 8
        val px = IntArray(w * h)
        for (y in 0 until h) {
            for (x in 0 until w) {
                val white = ((bits[y * stride + (x shr 3)].toInt() shr (7 - (x and 7))) and 1) == 1
                px[y * w + x] = if (white) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
            }
        }
        return Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888)
    }

    /** 成对 [count][value]，value 只能是 0x00 / 0xFF，count 1..255 */
    fun rleEncode(bits: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
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
}
