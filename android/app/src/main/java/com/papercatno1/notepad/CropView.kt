package com.papercatno1.notepad

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import kotlin.math.max
import kotlin.math.min

/**
 * 取景框：框的比例固定成屏幕的 320:212，图片可以拖动、双指缩放，
 * 框内所见即设备上看到的（缩到 320x212 后二值化）。
 */
class CropView @JvmOverloads constructor(
    ctx: Context,
    attrs: AttributeSet? = null
) : View(ctx, attrs) {

    private var bmp: Bitmap? = null
    private var scale = 1f
    private var tx = 0f
    private var ty = 0f
    private var minScale = 1f

    private val frame = RectF()
    private val matrix = Matrix()

    private val coverPaint = Paint().apply { color = 0xB0000000.toInt() }
    private val linePaint = Paint().apply {
        style = Paint.Style.STROKE
        strokeWidth = 4f
        color = 0xFF00E676.toInt()
    }

    private val scaleDetector = ScaleGestureDetector(ctx, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(detector: ScaleGestureDetector): Boolean {
            val b = bmp ?: return false
            val old = scale
            val next = (scale * detector.scaleFactor).coerceIn(minScale, minScale * 10f)
            if (next == old) return true
            // 以手指为中心缩放
            val fx = detector.focusX
            val fy = detector.focusY
            tx = fx - (fx - tx) * (next / old)
            ty = fy - (fy - ty) * (next / old)
            scale = next
            clampTranslate()
            invalidate()
            return true
        }
    })

    fun setBitmap(b: Bitmap) {
        bmp = b
        requestLayout()
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        val aspect = 320f / 212f
        var fw = w * 0.94f
        var fh = fw / aspect
        if (fh > h * 0.72f) {
            fh = h * 0.72f
            fw = fh * aspect
        }
        val left = (w - fw) / 2f
        val top = (h - fh) / 2f
        frame.set(left, top, left + fw, top + fh)
        fitImage()
    }

    /** 初始：铺满取景框（cover），居中 */
    private fun fitImage() {
        val b = bmp ?: return
        if (frame.width() <= 0f || frame.height() <= 0f) return
        minScale = max(frame.width() / b.width, frame.height() / b.height)
        scale = minScale
        tx = frame.centerX() - b.width * scale / 2f
        ty = frame.centerY() - b.height * scale / 2f
        clampTranslate()
        invalidate()
    }

    private fun shownRect(): RectF {
        val b = bmp ?: return RectF()
        return RectF(0f, 0f, b.width * scale, b.height * scale).apply { offset(tx, ty) }
    }

    private fun clampTranslate() {
        val r = shownRect()
        if (r.width() <= 0f) return
        if (r.left > frame.left) tx -= r.left - frame.left
        if (r.right < frame.right) tx += frame.right - r.right
        if (r.top > frame.top) ty -= r.top - frame.top
        if (r.bottom < frame.bottom) ty += frame.bottom - r.bottom
    }

    private var lastX = 0f
    private var lastY = 0f

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (bmp == null) return false
        scaleDetector.onTouchEvent(e)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastX = e.x
                lastY = e.y
                parent?.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_MOVE -> if (!scaleDetector.isInProgress) {
                tx += e.x - lastX
                ty += e.y - lastY
                lastX = e.x
                lastY = e.y
                clampTranslate()
                invalidate()
            }
        }
        return true
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(0xFF000000.toInt())
        val b = bmp ?: return
        matrix.reset()
        matrix.postScale(scale, scale)
        matrix.postTranslate(tx, ty)
        canvas.drawBitmap(b, matrix, null)

        canvas.drawRect(0f, 0f, width.toFloat(), frame.top, coverPaint)
        canvas.drawRect(0f, frame.bottom, width.toFloat(), height.toFloat(), coverPaint)
        canvas.drawRect(0f, frame.top, frame.left, frame.bottom, coverPaint)
        canvas.drawRect(frame.right, frame.top, width.toFloat(), frame.bottom, coverPaint)
        canvas.drawRect(frame, linePaint)
    }

    /** 取景框里那一块 -> 320x212（设备屏幕尺寸） */
    fun cropToScreen(): Bitmap? {
        val b = bmp ?: return null
        val r = shownRect()
        if (r.width() <= 0f || r.height() <= 0f) return null
        val k = b.width.toFloat() / r.width()
        val x = ((frame.left - r.left) * k).toInt().coerceIn(0, max(0, b.width - 1))
        val y = ((frame.top - r.top) * k).toInt().coerceIn(0, max(0, b.height - 1))
        val w = (frame.width() * k).toInt().coerceIn(1, b.width - x)
        val h = (frame.height() * k).toInt().coerceIn(1, b.height - y)
        val cut = try {
            Bitmap.createBitmap(b, x, y, w, h)
        } catch (e: Exception) {
            return null
        }
        return Bitmap.createScaledBitmap(cut, 320, 212, true)
    }

}
