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
 * Answers discovery broadcasts from melonDS so the Switch can list this TV.
 *
 * Query (8 bytes, little endian): u32 magic "MDSQ", u32 version.
 * Reply: u32 magic "MDSR", u16 stream port, u16 name length, UTF-8 name.
 */
class DiscoveryResponder(
    private val discoveryPort: Int,
    private val streamPort: Int,
    private val name: String,
) {
    companion object {
        const val QUERY_MAGIC = 0x5153444D
        const val REPLY_MAGIC = 0x5253444D
    }

    private val running = AtomicBoolean(false)
    private var thread: Thread? = null
    private var socket: DatagramSocket? = null

    fun start() {
        if (running.getAndSet(true)) return
        thread = Thread(::run, "discovery-responder").also { it.isDaemon = true; it.start() }
    }

    fun stop() {
        running.set(false)
        socket?.close()
        thread?.join(500)
        thread = null
    }

    private fun run() {
        val nameBytes = name.toByteArray(Charsets.UTF_8).copyOf(minOf(name.toByteArray(Charsets.UTF_8).size, 63))
        val reply = ByteBuffer.allocate(8 + nameBytes.size).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(REPLY_MAGIC)
            .putShort(streamPort.toShort())
            .putShort(nameBytes.size.toShort())
            .put(nameBytes)
            .array()

        val buf = ByteArray(64)
        val packet = DatagramPacket(buf, buf.size)
        try {
            DatagramSocket(null).use { sock ->
                socket = sock
                sock.reuseAddress = true
                sock.broadcast = true
                sock.bind(java.net.InetSocketAddress(discoveryPort))
                sock.soTimeout = 500
                while (running.get()) {
                    try {
                        sock.receive(packet)
                    } catch (e: SocketTimeoutException) {
                        continue
                    }
                    if (packet.length < 8) continue
                    val magic = ByteBuffer.wrap(buf, 0, 4).order(ByteOrder.LITTLE_ENDIAN).int
                    if (magic != QUERY_MAGIC) continue
                    sock.send(DatagramPacket(reply, reply.size, packet.socketAddress))
                }
            }
        } catch (e: Exception) {
            if (running.get()) e.printStackTrace()
        } finally {
            socket = null
        }
    }
}
