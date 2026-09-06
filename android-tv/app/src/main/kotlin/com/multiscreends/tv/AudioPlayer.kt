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

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.util.Log
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Plays the PCM packets of the stream ("MDSA"). Packets are queued from the
 * receiver thread and written to an AudioTrack from a dedicated thread, so a
 * slow audio write never delays video.
 *
 * The DS runs at 32823 Hz, which some devices refuse; in that case the audio
 * is resampled to 48 kHz with linear interpolation.
 */
class AudioPlayer {
    private class Chunk(val sequence: Long, val sampleRate: Int, val pcm: ByteArray)

    private val queue = ArrayBlockingQueue<Chunk>(64)
    private val running = AtomicBoolean(false)
    private var thread: Thread? = null
    private var lastSequence = -1L
    @Volatile var dropped = 0L
        private set
    @Volatile var failed = false
        private set
    @Volatile var trackRate = 0
        private set

    /** When false, packets are accepted but nothing is played. */
    @Volatile var enabled = true

    /** Target buffer in milliseconds; applied when the track is (re)created. */
    @Volatile var bufferMs = 120
        set(value) { if (field != value) { field = value; recreate = true } }
    @Volatile private var recreate = false

    fun start() {
        if (running.getAndSet(true)) return
        thread = Thread(::run, "audio-player").also { it.isDaemon = true; it.start() }
    }

    fun stop() {
        running.set(false)
        thread?.join(500)
        thread = null
        queue.clear()
    }

    /** Called from the receiver thread. */
    fun submit(sequence: Long, sampleRate: Int, pcm: ByteArray) {
        if (sequence <= lastSequence && lastSequence - sequence < 1000) {
            dropped++ // late or duplicate packet
            return
        }
        lastSequence = sequence
        if (!queue.offer(Chunk(sequence, sampleRate, pcm))) {
            // the player is behind: drop the oldest to keep latency bounded
            queue.poll()
            queue.offer(Chunk(sequence, sampleRate, pcm))
            dropped++
        }
    }

    private fun run() {
        var track: AudioTrack? = null
        var sourceRate = 0
        val resampler = Resampler()
        try {
            while (running.get()) {
                val chunk = queue.poll(200, TimeUnit.MILLISECONDS) ?: continue
                if (!enabled) {
                    if (track != null) { track.release(); track = null; trackRate = 0 }
                    continue
                }
                if (track == null || sourceRate != chunk.sampleRate || recreate) {
                    recreate = false
                    track?.release()
                    track = null
                    sourceRate = chunk.sampleRate
                    trackRate = 0
                    for (rate in intArrayOf(chunk.sampleRate, 48000, 44100)) {
                        val t = tryCreateTrack(rate) ?: continue
                        track = t
                        trackRate = rate
                        break
                    }
                    if (track == null) {
                        Log.e("AudioPlayer", "no usable AudioTrack, audio disabled")
                        failed = true
                        return
                    }
                    Log.i("AudioPlayer", "audio $sourceRate Hz -> track $trackRate Hz")
                    resampler.reset()
                    track.play()
                }
                val pcm = if (trackRate == sourceRate) chunk.pcm else resampler.convert(chunk.pcm, sourceRate, trackRate)
                if (pcm.isNotEmpty()) track.write(pcm, 0, pcm.size)
            }
        } catch (e: Exception) {
            Log.e("AudioPlayer", "audio thread stopped", e)
            failed = true
        } finally {
            track?.release()
        }
    }

    private fun tryCreateTrack(sampleRate: Int): AudioTrack? {
        return try {
            val minSize = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
            if (minSize <= 0) return null
            // enough buffer to ride out wifi jitter, small enough to stay in sync with the video
            val wanted = sampleRate * 2 * 2 * bufferMs / 1000
            val size = (maxOf(minSize, wanted) + 3) / 4 * 4
            val format = AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(sampleRate)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                .build()
            val track = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(format)
                .setBufferSizeInBytes(size)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
            if (track.state != AudioTrack.STATE_INITIALIZED) {
                track.release()
                null
            } else {
                track
            }
        } catch (e: Exception) {
            Log.w("AudioPlayer", "AudioTrack at $sampleRate Hz refused: ${e.message}")
            null
        }
    }

    /** Linear interpolation resampler for interleaved stereo s16, keeps phase between chunks. */
    private class Resampler {
        private var position = 0.0
        private var lastL = 0
        private var lastR = 0

        fun reset() { position = 0.0; lastL = 0; lastR = 0 }

        fun convert(pcm: ByteArray, from: Int, to: Int): ByteArray {
            val inFrames = pcm.size / 4
            if (inFrames == 0) return ByteArray(0)
            val step = from.toDouble() / to
            val outFrames = ((inFrames - position) / step).toInt().coerceAtLeast(0)
            val out = ByteArray(outFrames * 4)
            var o = 0
            var pos = position
            for (i in 0 until outFrames) {
                val idx = pos.toInt()
                val frac = pos - idx
                val l0 = if (idx >= 1) sample(pcm, idx - 1, 0) else lastL
                val r0 = if (idx >= 1) sample(pcm, idx - 1, 1) else lastR
                val l1 = sample(pcm, idx, 0)
                val r1 = sample(pcm, idx, 1)
                val l = (l0 + (l1 - l0) * frac).toInt()
                val r = (r0 + (r1 - r0) * frac).toInt()
                out[o++] = (l and 0xFF).toByte(); out[o++] = ((l shr 8) and 0xFF).toByte()
                out[o++] = (r and 0xFF).toByte(); out[o++] = ((r shr 8) and 0xFF).toByte()
                pos += step
            }
            position = pos - inFrames
            lastL = sample(pcm, inFrames - 1, 0)
            lastR = sample(pcm, inFrames - 1, 1)
            return out
        }

        private fun sample(pcm: ByteArray, frame: Int, channel: Int): Int {
            val i = frame * 4 + channel * 2
            return ((pcm[i + 1].toInt() shl 8) or (pcm[i].toInt() and 0xFF))
        }
    }
}
