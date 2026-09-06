#!/usr/bin/env python3
# Copyright 2026 Lucas Sarazin. Part of melonDS Switch TV Stream, GPL-3.0-or-later.
"""Test receiver for the melonDS Switch top-screen stream.

Listens for UDP datagrams (see Stream.cpp for the wire format), reassembles
JPEG frames and shows them in a window (needs Pillow). Without Pillow it
only prints statistics and writes the latest frame to latest.jpg.

Usage: stream_receiver.py [--port 9797] [--scale 3] [--save-dir DIR]
"""

import argparse
import socket
import struct
import sys
import time

MAGIC = 0x3153444D  # "MDS1" JPEG
MAGIC_QOI = 0x3253444D  # "MDS2" lossless
MAGIC_AUDIO = 0x4153444D  # "MDSA" PCM audio
HEADER = struct.Struct("<IIIIHH")  # magic, frame id, total size, part offset, part index, part count

DISCOVERY_PORT = 9798
QUERY_MAGIC = 0x5153444D  # "MDSQ"
REPLY_MAGIC = 0x5253444D  # "MDSR"


def discovery_responder(stream_port, name):
    """Answer melonDS discovery broadcasts so this receiver shows up in the Switch menu."""
    import threading

    def run():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", DISCOVERY_PORT))
        name_bytes = name.encode("utf-8")[:63]
        reply = struct.pack("<IHH", REPLY_MAGIC, stream_port, len(name_bytes)) + name_bytes
        while True:
            data, addr = sock.recvfrom(64)
            if len(data) >= 4 and struct.unpack_from("<I", data)[0] == QUERY_MAGIC:
                sock.sendto(reply, addr)

    threading.Thread(target=run, name="discovery", daemon=True).start()


def qoi_decode(d):
    """Decode a QOI image to (width, height, RGB bytes). Slow but fine for a test tool."""
    if d[:4] != b"qoif":
        return None
    w, h = struct.unpack_from(">II", d, 4)
    out = bytearray(w * h * 3)
    index = [(0, 0, 0, 0)] * 64
    r = g = b = 0
    a = 255
    p = 14
    pos = 0
    run = 0
    n = w * h
    end = len(d) - 8
    while pos < n:
        if run > 0:
            run -= 1
        elif p < end:
            b1 = d[p]
            p += 1
            if b1 == 0xFE:
                r, g, b = d[p], d[p + 1], d[p + 2]
                p += 3
            elif b1 == 0xFF:
                r, g, b, a = d[p], d[p + 1], d[p + 2], d[p + 3]
                p += 4
            else:
                op = b1 & 0xC0
                if op == 0x00:
                    r, g, b, a = index[b1 & 0x3F]
                elif op == 0x40:
                    r = (r + ((b1 >> 4) & 3) - 2) & 0xFF
                    g = (g + ((b1 >> 2) & 3) - 2) & 0xFF
                    b = (b + (b1 & 3) - 2) & 0xFF
                elif op == 0x80:
                    b2 = d[p]
                    p += 1
                    vg = (b1 & 0x3F) - 32
                    r = (r + vg - 8 + ((b2 >> 4) & 0x0F)) & 0xFF
                    g = (g + vg) & 0xFF
                    b = (b + vg - 8 + (b2 & 0x0F)) & 0xFF
                else:
                    run = b1 & 0x3F
            index[(r * 3 + g * 5 + b * 7 + a * 11) % 64] = (r, g, b, a)
        else:
            return None
        out[pos * 3] = r
        out[pos * 3 + 1] = g
        out[pos * 3 + 2] = b
        pos += 1
    return w, h, bytes(out)


class FrameAssembler:
    def __init__(self):
        self.frames = {}  # frame id -> [buffer, received parts, part count]
        self.dropped_partial = 0

    def push(self, data):
        if len(data) < HEADER.size:
            return None
        magic, frame_id, total, offset, index, count = HEADER.unpack_from(data)
        if magic not in (MAGIC, MAGIC_QOI):
            return None
        payload = data[HEADER.size:]

        entry = self.frames.get(frame_id)
        if entry is None:
            entry = [bytearray(total), set(), count]
            self.frames[frame_id] = entry
            # drop stale partial frames so memory stays bounded
            for old in [f for f in self.frames if f < frame_id - 8]:
                del self.frames[old]
                self.dropped_partial += 1

        buf, parts, _ = entry
        buf[offset:offset + len(payload)] = payload
        parts.add(index)
        if len(parts) == count:
            del self.frames[frame_id]
            return frame_id, bytes(buf)
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=9797)
    parser.add_argument("--scale", type=int, default=3)
    parser.add_argument("--save-dir", default=None, help="also write every frame as JPEG into this directory")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    sock.bind(("0.0.0.0", args.port))
    sock.setblocking(False)
    print(f"listening on udp/{args.port}")
    discovery_responder(args.port, socket.gethostname().split(".")[0] + " (Python receiver)")

    try:
        import tkinter as tk
        from PIL import Image, ImageTk
        import io
        have_gui = True
    except ImportError:
        have_gui = False
        print("Pillow/tkinter not available: stats only, latest frame written to latest.jpg")

    assembler = FrameAssembler()
    stats = {"frames": 0, "bytes": 0, "last_id": None, "lost": 0, "packets": 0, "partial": 0, "audio": 0, "t0": time.time()}

    if have_gui:
        root = tk.Tk()
        root.title("melonDS top screen")
        label = tk.Label(root)
        label.pack()
        photo = [None]

    def completed(frame_id, jpeg):
        # called for every fully reassembled frame
        if stats["last_id"] is not None and frame_id > stats["last_id"] + 1:
            stats["lost"] += frame_id - stats["last_id"] - 1
        stats["last_id"] = frame_id
        stats["frames"] += 1
        stats["bytes"] += len(jpeg)
        if args.save_dir:
            with open(f"{args.save_dir}/frame_{frame_id:08d}.jpg", "wb") as f:
                f.write(jpeg)

    def show(frame_id, jpeg):
        # called with the newest complete frame of this pump only
        if have_gui:
            if jpeg[:4] == b"qoif":
                decoded = qoi_decode(jpeg)
                if decoded is None:
                    return
                w, h, rgb = decoded
                img = Image.frombytes("RGB", (w, h), rgb)
            else:
                img = Image.open(io.BytesIO(jpeg))
            if args.scale != 1:
                img = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
            photo[0] = ImageTk.PhotoImage(img)
            label.configure(image=photo[0])
        elif stats["frames"] % 30 == 0:
            with open("latest.jpg", "wb") as f:
                f.write(jpeg)

    def report():
        elapsed = time.time() - stats["t0"]
        if elapsed >= 2.0:
            fps = stats["frames"] / elapsed
            kbps = stats["bytes"] * 8 / 1000 / elapsed
            pps = stats["packets"] / elapsed
            print(f"{fps:5.1f} fps  {kbps:7.0f} kbit/s  {pps:6.0f} pkt/s  audio pkts: {stats['audio']}  skipped ids: {stats['lost']}  incomplete frames dropped: {stats['partial']}")
            stats.update(frames=0, bytes=0, lost=0, packets=0, partial=0, audio=0, t0=time.time())

    def pump():
        # drain everything that is waiting (bounded so the GUI stays responsive),
        # display only the newest complete frame
        newest = None
        for _ in range(2000):
            try:
                data, _ = sock.recvfrom(65535)
            except BlockingIOError:
                break
            stats["packets"] += 1
            if len(data) >= 4 and struct.unpack_from("<I", data)[0] == MAGIC_AUDIO:
                stats["audio"] += 1
                continue
            done = assembler.push(data)
            if done:
                completed(*done)
                newest = done
        if newest:
            show(*newest)
        stats["partial"] += assembler.dropped_partial
        assembler.dropped_partial = 0
        report()

    if have_gui:
        def tick():
            pump()
            root.after(5, tick)
        root.after(5, tick)
        root.mainloop()
    else:
        while True:
            pump()
            time.sleep(0.002)


if __name__ == "__main__":
    sys.exit(main())
