package com.papercatno1.notepad

import android.graphics.Bitmap
import java.io.ByteArrayOutputStream

/** 位图 -> 1-bit -> RLE，格式与固件 notes.cpp 保持一致 */
object ImgCodec {

    /** 1 = 白、0 = 黑；每行按字节对齐，MSB 在左 */
    fun toBits(bmp: Bitmap): ByteArray {
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
                if (lum >= 128) {
                    out[base + (x shr 3)] =
                        (out[base + (x shr 3)].toInt() or (0x80 shr (x and 7))).toByte()
                }
            }
        }
        return out
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

    /** 由 RLE 反推一张预览位图（调试用，不参与发送） */
    fun rleDecode(rle: ByteArray?, w: Int, h: Int): Bitmap? {
        if (rle == null) return null
        val stride = (w + 7) / 8
        val bits = ByteArray(stride * h)
        var p = 0
        var o = 0
        while (p + 1 < rle.size && o < bits.size) {
            val n = rle[p].toInt() and 0xFF
            val v = rle[p + 1]
            for (k in 0 until n) {
                if (o >= bits.size) break
                bits[o++] = v
            }
            p += 2
        }
        val px = IntArray(w * h)
        for (y in 0 until h) {
            for (x in 0 until w) {
                val white = ((bits[y * stride + (x shr 3)].toInt() shr (7 - (x and 7))) and 1) == 1
                px[y * w + x] = if (white) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
            }
        }
        return Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888)
    }
}
