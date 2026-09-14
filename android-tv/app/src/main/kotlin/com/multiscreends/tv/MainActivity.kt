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

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import java.net.Inet4Address
import java.net.NetworkInterface

class MainActivity : Activity() {

    companion object {
        const val PORT = 9797
        const val DISCOVERY_PORT = 9798
        const val VERSION = "0.2"
    }

    private lateinit var prefs: Prefs
    private lateinit var view: StreamView
    private lateinit var menu: MenuView
    private var receiver: StreamReceiver? = null
    private var responder: DiscoveryResponder? = null
    private val audio = AudioPlayer()
    private val handler = Handler(Looper.getMainLooper())
    private var wifiLock: android.net.wifi.WifiManager.WifiLock? = null

    private var lastStatsFrames = 0L
    private var lastStatsBytes = 0L
    private var lastStatsTime = 0L
    private var fps = 0.0
    private var kbps = 0.0

    private val statsTick = object : Runnable {
        override fun run() {
            updateStatus()
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        prefs = Prefs(this)

        view = StreamView(this)
        view.scaling = prefs.scaling
        menu = MenuView(this).apply { visibility = View.GONE }
        buildMenu()

        val root = FrameLayout(this)
        val match = ViewGroup.LayoutParams.MATCH_PARENT
        root.addView(view, FrameLayout.LayoutParams(match, match))
        root.addView(menu, FrameLayout.LayoutParams(match, match))
        setContentView(root)
        hideSystemUi()

        audio.enabled = prefs.audioEnabled
        audio.bufferMs = prefs.audioLatency.bufferMs
    }

    private fun buildMenu() {
        menu.title = "Settings"
        menu.rows = listOf(
            MenuView.Row(
                "Display",
                { Prefs.Scaling.values().map { it.label } },
                { prefs.scaling.ordinal },
                { i -> prefs.scaling = Prefs.Scaling.values()[i]; view.scaling = prefs.scaling },
            ),
            MenuView.Row(
                "Audio",
                { listOf("Off", "On") },
                { if (prefs.audioEnabled) 1 else 0 },
                { i -> prefs.audioEnabled = i == 1; audio.enabled = prefs.audioEnabled },
            ),
            MenuView.Row(
                "Audio latency",
                { Prefs.AudioLatency.values().map { it.label } },
                { prefs.audioLatency.ordinal },
                { i -> prefs.audioLatency = Prefs.AudioLatency.values()[i]; audio.bufferMs = prefs.audioLatency.bufferMs },
            ),
            MenuView.Row(
                "Statistics overlay",
                { listOf("Hidden", "Shown") },
                { if (prefs.showStats) 1 else 0 },
                { i -> prefs.showStats = i == 1; updateStatus() },
            ),
        )
        menu.footer = listOf(
            "melonDS TV $VERSION   listening on UDP $PORT, discovery on $DISCOVERY_PORT",
            "Back closes this menu",
        )
        menu.onClose = { view.requestFocus() }
    }

    override fun onStart() {
        super.onStart()
        audio.start()
        receiver = StreamReceiver(
            PORT,
            onFrame = { _, codec, data, length -> view.submit(codec, data, length) },
            onAudio = { seq, rate, pcm -> audio.submit(seq, rate, pcm) },
        ).also { it.start() }
        responder = DiscoveryResponder(DISCOVERY_PORT, PORT, deviceName()).also { it.start() }
        lastStatsTime = System.nanoTime()
        view.waitingTitle = deviceName()
        acquireWifiLock()
        handler.post(statsTick)
        updateStatus()
    }

    /** Keeps the wifi radio out of power saving while the app is in front: the
     *  periodic sleeps otherwise show up as small regular hiccups in the stream. */
    private fun acquireWifiLock() {
        try {
            val wm = applicationContext.getSystemService(WIFI_SERVICE) as android.net.wifi.WifiManager
            @Suppress("DEPRECATION")
            val mode = if (android.os.Build.VERSION.SDK_INT >= 29)
                android.net.wifi.WifiManager.WIFI_MODE_FULL_LOW_LATENCY
            else android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF
            wifiLock = wm.createWifiLock(mode, "melonds-tv").also { it.setReferenceCounted(false); it.acquire() }
        } catch (e: Exception) {
            android.util.Log.w("MainActivity", "wifi lock unavailable: ${e.message}")
        }
    }

    override fun onStop() {
        handler.removeCallbacks(statsTick)
        receiver?.stop()
        receiver = null
        responder?.stop()
        responder = null
        audio.stop()
        try { wifiLock?.release() } catch (e: Exception) { }
        wifiLock = null
        super.onStop()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (menu.visibility == View.VISIBLE) return menu.onKeyDown(keyCode, event) || super.onKeyDown(keyCode, event)
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_MENU, KeyEvent.KEYCODE_INFO,
            KeyEvent.KEYCODE_SETTINGS -> {
                menu.open()
                return true
            }
        }
        return super.onKeyDown(keyCode, event)
    }

    private fun updateStatus() {
        val r = receiver ?: return
        val now = System.nanoTime()
        val seconds = (now - lastStatsTime) / 1e9
        if (seconds >= 0.5) {
            val frames = r.stats.frames
            val bytes = r.stats.bytes
            fps = (frames - lastStatsFrames) / seconds
            kbps = (bytes - lastStatsBytes) * 8 / 1000 / seconds
            lastStatsFrames = frames
            lastStatsBytes = bytes
            lastStatsTime = now
        }

        val receiving = r.stats.lastFrameNanos != 0L && now - r.stats.lastFrameNanos < 2_000_000_000L
        view.receiving = receiving

        if (!receiving) {
            val addresses = localAddresses().joinToString("   ").ifEmpty { "no network connection" }
            view.waitingLines = listOf(
                "Address: $addresses",
                "On the Switch: melonDS > Settings > Display > Top screen streaming,",
                "then pick this TV in the list.",
                "Press OK or Menu for settings.",
            )
        }

        view.statsLine = if (prefs.showStats && receiving) {
            val codec = when (r.stats.codec) { StreamReceiver.Codec.QOI -> "lossless"; StreamReceiver.Codec.JPEG -> "JPEG"; null -> "-" }
            val audioState = when {
                !prefs.audioEnabled -> "audio off"
                audio.failed -> "audio unavailable"
                r.stats.audioPackets > 0 -> "audio ${audio.trackRate} Hz"
                else -> "audio on Switch"
            }
            val line = String.format("%.0f fps   %.0f kbit/s   %s   %s   lost %d   net gap %d ms   draw gap %d ms   %s",
                fps, kbps, codec, audioState, r.stats.incomplete, r.stats.maxGapMs, view.maxDrawGapMs,
                prefs.scaling.label.lowercase())
            r.stats.resetGap()
            view.maxDrawGapMs = 0
            line
        } else null
    }

    private fun deviceName(): String {
        val name = try {
            android.provider.Settings.Global.getString(contentResolver, android.provider.Settings.Global.DEVICE_NAME)
        } catch (e: Exception) {
            null
        }
        return if (name.isNullOrBlank()) android.os.Build.MODEL else name
    }

    private fun localAddresses(): List<String> = try {
        NetworkInterface.getNetworkInterfaces().toList()
            .filter { it.isUp && !it.isLoopback }
            .flatMap { it.inetAddresses.toList() }
            .filterIsInstance<Inet4Address>()
            .map { it.hostAddress ?: "" }
    } catch (e: Exception) {
        emptyList()
    }

    private fun hideSystemUi() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            )
    }
}
