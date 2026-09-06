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

import android.graphics.Bitmap

/** Minimal QOI decoder (https://qoiformat.org), enough for the lossless stream. */
object Qoi {
    private const val OP_INDEX = 0x00
    private const val OP_DIFF = 0x40
    private const val OP_LUMA = 0x80
    private const val OP_RUN = 0xC0
    private const val OP_RGB = 0xFE
    private const val OP_RGBA = 0xFF
    private const val MASK = 0xC0

    class Image(val width: Int, val height: Int, val pixels: IntArray)

    private fun u32be(d: ByteArray, o: Int) =
        ((d[o].toInt() and 0xFF) shl 24) or ((d[o + 1].toInt() and 0xFF) shl 16) or
            ((d[o + 2].toInt() and 0xFF) shl 8) or (d[o + 3].toInt() and 0xFF)

    /** Returns null if the data is not a valid QOI file. Pixels are ARGB (Bitmap order). */
    fun decode(d: ByteArray, reuse: IntArray? = null): Image? {
        if (d.size < 22) return null
        if (d[0] != 'q'.code.toByte() || d[1] != 'o'.code.toByte() || d[2] != 'i'.code.toByte() || d[3] != 'f'.code.toByte()) return null
        val width = u32be(d, 4)
        val height = u32be(d, 8)
        if (width <= 0 || height <= 0 || width * height > 4096 * 4096) return null

        val count = width * height
        val out = if (reuse != null && reuse.size == count) reuse else IntArray(count)
        val index = IntArray(64)

        var r = 0; var g = 0; var b = 0; var a = 255
        var p = 14
        var pos = 0
        val end = d.size - 8 // 8 byte end marker
        var run = 0

        while (pos < count) {
            if (run > 0) {
                run--
            } else if (p < end) {
                val b1 = d[p++].toInt() and 0xFF
                when {
                    b1 == OP_RGB -> {
                        r = d[p++].toInt() and 0xFF; g = d[p++].toInt() and 0xFF; b = d[p++].toInt() and 0xFF
                    }
                    b1 == OP_RGBA -> {
                        r = d[p++].toInt() and 0xFF; g = d[p++].toInt() and 0xFF; b = d[p++].toInt() and 0xFF; a = d[p++].toInt() and 0xFF
                    }
                    (b1 and MASK) == OP_INDEX -> {
                        val px = index[b1 and 0x3F]
                        a = (px ushr 24) and 0xFF; r = (px ushr 16) and 0xFF; g = (px ushr 8) and 0xFF; b = px and 0xFF
                    }
                    (b1 and MASK) == OP_DIFF -> {
                        r = (r + ((b1 shr 4) and 0x03) - 2) and 0xFF
                        g = (g + ((b1 shr 2) and 0x03) - 2) and 0xFF
                        b = (b + (b1 and 0x03) - 2) and 0xFF
                    }
                    (b1 and MASK) == OP_LUMA -> {
                        val b2 = d[p++].toInt() and 0xFF
                        val vg = (b1 and 0x3F) - 32
                        r = (r + vg - 8 + ((b2 shr 4) and 0x0F)) and 0xFF
                        g = (g + vg) and 0xFF
                        b = (b + vg - 8 + (b2 and 0x0F)) and 0xFF
                    }
                    else -> { // OP_RUN
                        run = b1 and 0x3F
                    }
                }
                index[(r * 3 + g * 5 + b * 7 + a * 11) % 64] = (a shl 24) or (r shl 16) or (g shl 8) or b
            } else {
                return null // truncated
            }
            out[pos++] = (a shl 24) or (r shl 16) or (g shl 8) or b
        }
        return Image(width, height, out)
    }

    fun toBitmap(img: Image, reuse: Bitmap?): Bitmap {
        val bmp = if (reuse != null && !reuse.isRecycled && reuse.width == img.width && reuse.height == img.height && reuse.isMutable)
            reuse else Bitmap.createBitmap(img.width, img.height, Bitmap.Config.ARGB_8888)
        bmp.setPixels(img.pixels, 0, img.width, 0, 0, img.width, img.height)
        return bmp
    }
}
