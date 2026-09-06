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
import android.content.SharedPreferences

/** User settings, persisted between launches. */
class Prefs(context: Context) {
    enum class Scaling(val label: String) {
        FILL("Fill screen (sharp)"),
        INTEGER("Pixel perfect"),
        SMOOTH("Fill screen (smooth)");
    }

    enum class AudioLatency(val label: String, val bufferMs: Int) {
        LOW("Low (60 ms)", 60),
        NORMAL("Normal (120 ms)", 120),
        HIGH("High (250 ms)", 250);
    }

    private val sp: SharedPreferences = context.getSharedPreferences("melonds_tv", Context.MODE_PRIVATE)

    var scaling: Scaling
        get() = enumOr(sp.getString("scaling", null), Scaling.FILL)
        set(v) = sp.edit().putString("scaling", v.name).apply()

    var showStats: Boolean
        get() = sp.getBoolean("show_stats", false)
        set(v) = sp.edit().putBoolean("show_stats", v).apply()

    var audioEnabled: Boolean
        get() = sp.getBoolean("audio_enabled", true)
        set(v) = sp.edit().putBoolean("audio_enabled", v).apply()

    var audioLatency: AudioLatency
        get() = enumOr(sp.getString("audio_latency", null), AudioLatency.NORMAL)
        set(v) = sp.edit().putString("audio_latency", v.name).apply()

    private inline fun <reified T : Enum<T>> enumOr(name: String?, default: T): T =
        enumValues<T>().firstOrNull { it.name == name } ?: default
}
