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

import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketTimeoutException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Receives the melonDS Switch top-screen stream.
 *
 * Video datagram (little endian), header + up to 1400 bytes of the image file:
 *   u32 magic "MDS1" (JPEG) or "MDS2" (QOI, lossless), u32 frameId, u32 totalSize,
 *   u32 partOffset, u16 partIndex, u16 partCount
 * Frames are reassembled by id; a frame is delivered once all its parts arrived.
 * Older incomplete frames are dropped so a lost packet only costs one frame.
 *
 * Audio datagram: u32 magic "MDSA", u32 sequence, u32 sampleRate, u16 channels,
 *   u16 frames, then interleaved s16 PCM.
 */
class StreamReceiver(
    private val port: Int,
    /** data is a pooled buffer: only the first `length` bytes are valid and it must not be kept. */
    private val onFrame: (frameId: Long, codec: Codec, data: ByteArray, length: Int) -> Unit,
    private val onAudio: (sequence: Long, sampleRate: Int, pcm: ByteArray) -> Unit,
) {
    enum class Codec { JPEG, QOI }

    companion object {
        const val MAGIC_JPEG = 0x3153444DL
        const val MAGIC_QOI = 0x3253444DL
        const val MAGIC_AUDIO = 0x4153444DL
        const val HEADER_SIZE = 20
        const val AUDIO_HEADER_SIZE = 16
        private const val MAX_PENDING = 8
    }

    class Stats {
        @Volatile var packets = 0L
        @Volatile var frames = 0L
        @Volatile var bytes = 0L
        @Volatile var incomplete = 0L
        @Volatile var audioPackets = 0L
        @Volatile var lastFrameNanos = 0L
        @Volatile var codec: Codec? = null
        /** Longest interval between two complete frames since the last reset, in ms. */
        @Volatile var maxGapMs = 0L
        fun resetGap() { maxGapMs = 0L }
    }

    val stats = Stats()

    private class Pending(val total: Int, val partCount: Int, val buffer: ByteArray) {
        val received = BooleanArray(partCount)
        var receivedCount = 0
    }

    // Reused frame buffers: allocating 20-60 KB per frame at 60 fps keeps the
    // garbage collector busy, which shows up as periodic hiccups.
    private val bufferPool = ArrayDeque<ByteArray>()
    private fun takeBuffer(size: Int): ByteArray {
        val b = bufferPool.removeFirstOrNull()
        return if (b != null && b.size >= size) b else ByteArray(maxOf(size, 96 * 1024))
    }
    private fun giveBackBuffer(b: ByteArray) {
        if (bufferPool.size < 16) bufferPool.addLast(b)
    }

    private val running = AtomicBoolean(false)
    private var thread: Thread? = null
    private var socket: DatagramSocket? = null

    fun start() {
        if (running.getAndSet(true)) return
        thread = Thread(::run, "stream-receiver").also { it.isDaemon = true; it.start() }
    }

    fun stop() {
        running.set(false)
        socket?.close()
        thread?.join(500)
        thread = null
    }

    private fun run() {
        val pending = sortedMapOf<Long, Pending>()
        val buf = ByteArray(65535)
        val packet = DatagramPacket(buf, buf.size)

        try {
            DatagramSocket(port).use { sock ->
                socket = sock
                sock.receiveBufferSize = 4 * 1024 * 1024
                sock.soTimeout = 500

                while (running.get()) {
                    try {
                        sock.receive(packet)
                    } catch (e: SocketTimeoutException) {
                        continue
                    }
                    stats.packets++
                    handle(buf, packet.length, pending)
                }
            }
        } catch (e: Exception) {
            if (running.get()) e.printStackTrace()
        } finally {
            socket = null
        }
    }

    private fun handle(data: ByteArray, length: Int, pending: java.util.SortedMap<Long, Pending>) {
        if (length < 4) return
        val magic = ByteBuffer.wrap(data, 0, 4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xFFFFFFFFL
        when (magic) {
            MAGIC_JPEG -> handleVideo(data, length, pending, Codec.JPEG)
            MAGIC_QOI -> handleVideo(data, length, pending, Codec.QOI)
            MAGIC_AUDIO -> handleAudio(data, length)
        }
    }

    private fun handleAudio(data: ByteArray, length: Int) {
        if (length < AUDIO_HEADER_SIZE) return
        val header = ByteBuffer.wrap(data, 4, AUDIO_HEADER_SIZE - 4).order(ByteOrder.LITTLE_ENDIAN)
        val sequence = header.int.toLong() and 0xFFFFFFFFL
        val sampleRate = header.int
        val channels = header.short.toInt() and 0xFFFF
        val frames = header.short.toInt() and 0xFFFF
        if (channels != 2 || sampleRate < 8000 || sampleRate > 96000) return
        val pcmLength = frames * channels * 2
        if (pcmLength <= 0 || AUDIO_HEADER_SIZE + pcmLength > length) return
        stats.audioPackets++
        onAudio(sequence, sampleRate, data.copyOfRange(AUDIO_HEADER_SIZE, AUDIO_HEADER_SIZE + pcmLength))
    }

    private fun handleVideo(data: ByteArray, length: Int, pending: java.util.SortedMap<Long, Pending>, codec: Codec) {
        if (length < HEADER_SIZE) return
        val header = ByteBuffer.wrap(data, 4, HEADER_SIZE - 4).order(ByteOrder.LITTLE_ENDIAN)
        val frameId = header.int.toLong() and 0xFFFFFFFFL
        val total = header.int
        val offset = header.int
        val index = header.short.toInt() and 0xFFFF
        val count = header.short.toInt() and 0xFFFF
        val payloadLength = length - HEADER_SIZE

        if (total <= 0 || total > 4 * 1024 * 1024 || count == 0 || index >= count) return
        if (offset < 0 || offset + payloadLength > total) return

        var entry = pending[frameId]
        if (entry == null) {
            entry = Pending(total, count, takeBuffer(total))
            pending[frameId] = entry
            // forget frames that are too old to ever complete
            val stale = pending.headMap(frameId - MAX_PENDING)
            stats.incomplete += stale.size
            for (old in stale.values) giveBackBuffer(old.buffer)
            stale.clear()
        }

        System.arraycopy(data, HEADER_SIZE, entry.buffer, offset, payloadLength)
        if (!entry.received[index]) {
            entry.received[index] = true
            entry.receivedCount++
        }

        if (entry.receivedCount == entry.partCount) {
            pending.remove(frameId)
            val now = System.nanoTime()
            if (stats.lastFrameNanos != 0L) {
                val gapMs = (now - stats.lastFrameNanos) / 1_000_000
                if (gapMs > stats.maxGapMs) stats.maxGapMs = gapMs
            }
            stats.frames++
            stats.bytes += total
            stats.lastFrameNanos = now
            stats.codec = codec
            onFrame(frameId, codec, entry.buffer, entry.total)
            giveBackBuffer(entry.buffer)
        }
    }
}
