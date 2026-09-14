/*
 * Copyright 2026 Lucas Sarazin
 *
 * This file is part of melonDS Switch TV Stream.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see http://www.gnu.org/licenses/.
 */

package com.multiscreends.tv

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.view.View

/**
 * Draws the latest decoded frame, or a waiting screen while no stream arrives,
 * plus an optional one-line statistics overlay.
 *
 * Scaling modes:
 *  - FILL: the image fills the screen height. It is first enlarged by an
 *    integer factor with sharp pixels, then stretched the last few percent
 *    with bilinear filtering ("sharp bilinear"): no black bars, no blur.
 *  - INTEGER: classic pixel-perfect integer scaling with black bars.
 *  - SMOOTH: plain bilinear scaling to the screen height.
 */
class StreamView(context: Context) : View(context) {

    var scaling = Prefs.Scaling.FILL
        set(value) { field = value; invalidate() }

    var statsLine: String? = null
        set(value) { field = value; postInvalidate() }

    /** Text shown on the waiting screen (device name, addresses, hints). */
    var waitingTitle = ""
    var waitingLines: List<String> = emptyList()
        set(value) { field = value; postInvalidate() }

    /** True while frames are arriving; switches between waiting screen and video. */
    var receiving = false
        set(value) { if (field != value) { field = value; postInvalidate() } }

    private var frame: Bitmap? = null
    private var qoiPixels: IntArray? = null
    private val decodeOptions = BitmapFactory.Options().apply {
        inMutable = true
        inPreferredConfig = Bitmap.Config.ARGB_8888
    }

    private var upscaled: Bitmap? = null
    private var upscaledCanvas: Canvas? = null
    private val logo: Bitmap? = try { BitmapFactory.decodeResource(resources, R.mipmap.ic_launcher) } catch (e: Exception) { null }

    private val nearestPaint = Paint().apply { isFilterBitmap = false }
    private val linearPaint = Paint().apply { isFilterBitmap = true }
    private val bgPaint = Paint().apply { color = Color.rgb(16, 20, 24) }
    private val cardPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(255, 24, 30, 38) }
    private val accentPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.rgb(220, 60, 60) }
    private val titlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 60f; isFakeBoldText = true }
    private val bigPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 40f }
    private val dimPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(255, 160, 166, 172); textSize = 30f }
    private val statsPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 26f
        setShadowLayer(4f, 0f, 0f, Color.BLACK)
    }
    private val dst = Rect()
    private val src = Rect()
    private val rectF = RectF()

    /** Called from the receiver thread. Decodes here, hands the bitmap to the UI thread. */
    // Two bitmaps: one being decoded into while the other is on screen.
    private val bitmaps = arrayOfNulls<Bitmap>(2)
    private var decodeIndex = 0

    /** Longest interval between two draws, in ms, since the last reset. */
    @Volatile var maxDrawGapMs = 0L
    private var lastDrawNanos = 0L

    fun submit(codec: StreamReceiver.Codec, data: ByteArray, length: Int) {
        val target = bitmaps[decodeIndex]
        val decoded: Bitmap = when (codec) {
            StreamReceiver.Codec.JPEG -> decodeJpeg(data, length, target) ?: return
            StreamReceiver.Codec.QOI -> {
                val img = Qoi.decode(data, qoiPixels, length) ?: return
                qoiPixels = img.pixels
                Qoi.toBitmap(img, target)
            }
        }
        bitmaps[decodeIndex] = decoded
        decodeIndex = 1 - decodeIndex
        post {
            frame = decoded
            invalidate()
        }
    }

    private fun decodeJpeg(jpeg: ByteArray, length: Int, reuse: Bitmap?): Bitmap? {
        decodeOptions.inBitmap = if (reuse != null && !reuse.isRecycled && reuse.isMutable) reuse else null
        return try {
            BitmapFactory.decodeByteArray(jpeg, 0, length, decodeOptions)
        } catch (e: IllegalArgumentException) {
            decodeOptions.inBitmap = null
            BitmapFactory.decodeByteArray(jpeg, 0, length, decodeOptions)
        }
    }

    override fun onDraw(canvas: Canvas) {
        val now = System.nanoTime()
        if (receiving && lastDrawNanos != 0L) {
            val gap = (now - lastDrawNanos) / 1_000_000
            if (gap > maxDrawGapMs) maxDrawGapMs = gap
        }
        lastDrawNanos = now

        val bmp = frame
        if (receiving && bmp != null && width > 0 && height > 0) {
            canvas.drawColor(Color.BLACK)
            drawFrame(canvas, bmp)
        } else {
            drawWaiting(canvas)
        }

        val stats = statsLine
        if (stats != null) canvas.drawText(stats, 32f, 44f, statsPaint)
    }

    private fun drawFrame(canvas: Canvas, bmp: Bitmap) {
        val intScale = minOf(width / bmp.width, height / bmp.height).coerceAtLeast(1)
        val factor = minOf(width.toFloat() / bmp.width, height.toFloat() / bmp.height)
        when (scaling) {
            Prefs.Scaling.INTEGER -> {
                val w = bmp.width * intScale
                val h = bmp.height * intScale
                dst.set((width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2)
                canvas.drawBitmap(bmp, null, dst, nearestPaint)
            }
            Prefs.Scaling.FILL -> {
                val up = upscaledFor(bmp.width * intScale, bmp.height * intScale)
                src.set(0, 0, bmp.width, bmp.height)
                dst.set(0, 0, up.width, up.height)
                upscaledCanvas!!.drawBitmap(bmp, src, dst, nearestPaint)
                val w = (bmp.width * factor).toInt()
                val h = (bmp.height * factor).toInt()
                dst.set((width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2)
                canvas.drawBitmap(up, null, dst, linearPaint)
            }
            Prefs.Scaling.SMOOTH -> {
                val w = (bmp.width * factor).toInt()
                val h = (bmp.height * factor).toInt()
                dst.set((width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2)
                canvas.drawBitmap(bmp, null, dst, linearPaint)
            }
        }
    }

    private fun drawWaiting(canvas: Canvas) {
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)

        val cardW = minOf(width * 0.6f, 1000f)
        val cardH = 300f + waitingLines.size * 44f
        val left = (width - cardW) / 2f
        val top = (height - cardH) / 2f
        rectF.set(left, top, left + cardW, top + cardH)
        canvas.drawRoundRect(rectF, 32f, 32f, cardPaint)
        rectF.set(left, top, left + cardW, top + 10f)
        canvas.drawRoundRect(rectF, 5f, 5f, accentPaint)

        var y = top + 60f
        logo?.let {
            dst.set((left + 50f).toInt(), (y).toInt(), (left + 50f + 96f).toInt(), (y + 96f).toInt())
            canvas.drawBitmap(it, null, dst, linearPaint)
        }
        canvas.drawText("melonDS TV", left + 170f, y + 50f, titlePaint)
        canvas.drawText("Nintendo DS top screen, streamed from your Switch", left + 170f, y + 92f, dimPaint)
        y += 170f

        canvas.drawText(waitingTitle, left + 50f, y, bigPaint)
        y += 56f
        for (line in waitingLines) {
            canvas.drawText(line, left + 50f, y, dimPaint)
            y += 44f
        }
    }

    private fun upscaledFor(w: Int, h: Int): Bitmap {
        val existing = upscaled
        if (existing != null && existing.width == w && existing.height == h) return existing
        existing?.recycle()
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        upscaled = bmp
        upscaledCanvas = Canvas(bmp)
        return bmp
    }
}
