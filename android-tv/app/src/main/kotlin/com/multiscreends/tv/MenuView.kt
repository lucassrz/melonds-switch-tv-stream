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
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.KeyEvent
import android.view.View

/**
 * A small settings panel drawn on the right side of the screen, driven by the
 * TV remote: up/down selects a row, left/right/OK changes the value, back closes.
 */
class MenuView(context: Context) : View(context) {

    class Row(val label: String, val values: () -> List<String>, val current: () -> Int, val onChange: (Int) -> Unit)

    var rows: List<Row> = emptyList()
    var title = "melonDS TV"
    var footer: List<String> = emptyList()
    var onClose: (() -> Unit)? = null

    private var selected = 0

    private val scrim = Paint().apply { color = Color.argb(140, 0, 0, 0) }
    private val card = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(235, 24, 30, 38) }
    private val highlight = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(255, 220, 60, 60) }
    private val titlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 44f; isFakeBoldText = true }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 30f }
    private val valuePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(255, 200, 205, 210); textSize = 30f; textAlign = Paint.Align.RIGHT }
    private val footerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(255, 140, 146, 152); textSize = 24f }
    private val rect = RectF()

    init {
        isFocusable = true
        isFocusableInTouchMode = true
    }

    fun open() {
        selected = 0
        visibility = VISIBLE
        requestFocus()
        invalidate()
    }

    fun close() {
        visibility = GONE
        onClose?.invoke()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (rows.isEmpty()) return false
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_UP -> selected = (selected - 1 + rows.size) % rows.size
            KeyEvent.KEYCODE_DPAD_DOWN -> selected = (selected + 1) % rows.size
            KeyEvent.KEYCODE_DPAD_LEFT -> step(-1)
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER -> step(1)
            KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_MENU, KeyEvent.KEYCODE_ESCAPE -> { close(); return true }
            else -> return super.onKeyDown(keyCode, event)
        }
        invalidate()
        return true
    }

    private fun step(delta: Int) {
        val row = rows[selected]
        val values = row.values()
        if (values.isEmpty()) return
        val next = ((row.current() + delta) % values.size + values.size) % values.size
        row.onChange(next)
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), scrim)

        val cardWidth = minOf(width * 0.42f, 760f)
        val pad = 40f
        val rowHeight = 68f
        val cardHeight = pad * 2 + 80f + rows.size * rowHeight + footer.size * 34f + 20f
        val left = width - cardWidth - 60f
        val top = (height - cardHeight) / 2f
        rect.set(left, top, left + cardWidth, top + cardHeight)
        canvas.drawRoundRect(rect, 28f, 28f, card)

        var y = top + pad + 44f
        canvas.drawText(title, left + pad, y, titlePaint)
        y += 56f

        for ((i, row) in rows.withIndex()) {
            val rowTop = y
            if (i == selected) {
                rect.set(left + pad - 16f, rowTop, left + cardWidth - pad + 16f, rowTop + rowHeight - 8f)
                canvas.drawRoundRect(rect, 14f, 14f, highlight)
            }
            val baseline = rowTop + rowHeight / 2f + 10f
            canvas.drawText(row.label, left + pad, baseline, labelPaint)
            val values = row.values()
            val value = values.getOrNull(row.current()) ?: ""
            canvas.drawText(if (i == selected) "‹ $value ›" else value, left + cardWidth - pad, baseline, valuePaint)
            y += rowHeight
        }

        y += 24f
        for (line in footer) {
            canvas.drawText(line, left + pad, y, footerPaint)
            y += 34f
        }
    }
}
