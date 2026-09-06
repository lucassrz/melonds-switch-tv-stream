/*
    Copyright 2026 Lucas Sarazin

    This file is part of melonDS Switch TV Stream, a fork of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
    A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/
#include "Stream.h"

#include <switch.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vector>

#include "PlatformConfig.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image/stb_image_write.h"

#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "qoi.h"

namespace Stream
{

const int ScreenWidth = 256;
const int ScreenHeight = 192;
const int FrameBytes = ScreenWidth * ScreenHeight * 4;

// Wire format: every UDP datagram starts with this header (little endian),
// followed by up to MaxPayload bytes of the JPEG file. A frame is complete
// once all PartCount parts with the same FrameId were received.
struct __attribute__((packed)) PacketHeader
{
    u32 Magic;      // "MDS1"
    u32 FrameId;
    u32 TotalSize;  // size of the whole JPEG
    u32 PartOffset; // offset of this part's payload inside the JPEG
    u16 PartIndex;
    u16 PartCount;
};
const u32 MagicJpeg = 0x3153444D; // "MDS1": payload is a JPEG file
const u32 MagicQoi  = 0x3253444D; // "MDS2": payload is a QOI file (lossless)
const int MaxPayload = 1400;
// lossless frames above this size fall back to JPEG in "Auto" mode
// (40 KB at 60 fps is about 20 Mbit/s)
const int AutoLosslessMaxBytes = 40 * 1024;

// Audio datagram: header then interleaved stereo s16 PCM.
struct __attribute__((packed)) AudioHeader
{
    u32 Magic;      // "MDSA"
    u32 Sequence;
    u32 SampleRate;
    u16 Channels;
    u16 Frames;     // sample frames in this packet
};
const u32 MagicAudio = 0x4153444D;
const int AudioFramesPerPacket = 320; // 1280 bytes of PCM, fits in one MTU

// Discovery: the Switch broadcasts a query on DiscoveryPort, receivers answer
// with their name. Both messages are little endian.
struct __attribute__((packed)) DiscoveryQuery
{
    u32 Magic;      // "MDSQ"
    u32 Version;
};
struct __attribute__((packed)) DiscoveryReplyHeader
{
    u32 Magic;      // "MDSR"
    u16 StreamPort;
    u16 NameLength; // UTF-8 name follows
};
const u32 DiscoveryQueryMagic = 0x5153444D;
const u32 DiscoveryReplyMagic = 0x5253444D;
const int DiscoveryPort = 9798;
const int MaxDevices = 16;

static Thread WorkerThread;
static bool WorkerRunning = false;
static Mutex Lock;
static CondVar Cond;
static bool Quit = false;

static u8 PendingFrame[FrameBytes];
static bool PendingValid = false;
static u32 PendingId = 0;

static int Sock = -1;
static sockaddr_in Dest;
static char CurHost[64] = "";
static int CurPort = 0;

static u32 FrameCounter = 0;
static u32 StatFramesSent = 0;
static u32 StatLastBytes = 0;
static u32 StatPacketsSent = 0;
static u32 StatSendErrors = 0;
static int StatLastErrno = 0;
static u32 StatEncodeUs = 0;
static u32 AudioSequence = 0;

static int DiscSock = -1;
static u64 DiscLastQuery = 0;
static u64 DiscLastTick = 0;
static Device Devices[MaxDevices];
static int DeviceCount = 0;
static bool NifmReady = false;

static u64 NowNs()
{
    return armTicksToNs(armGetSystemTick());
}

static void WriteFunc(void* ctx, void* data, int size)
{
    std::vector<u8>* out = (std::vector<u8>*)ctx;
    out->insert(out->end(), (u8*)data, (u8*)data + size);
}

static void CloseSocket()
{
    if (Sock >= 0)
    {
        close(Sock);
        Sock = -1;
    }
}

static bool OpenSocket(const char* host, int port)
{
    CloseSocket();

    memset(&Dest, 0, sizeof(Dest));
    Dest.sin_family = AF_INET;
    Dest.sin_port = htons(port);
    if (host[0] == '\0' || inet_aton(host, &Dest.sin_addr) == 0)
    {
        printf("Stream: invalid host '%s'\n", host);
        return false;
    }

    Sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (Sock < 0)
    {
        printf("Stream: socket() failed\n");
        return false;
    }

    printf("Stream: sending to %s:%d\n", host, port);
    return true;
}

static void SendFrame(u32 id, const u8* data, u32 total, u32 magic)
{
    u8 packet[sizeof(PacketHeader) + MaxPayload];
    PacketHeader* header = (PacketHeader*)packet;

    u16 partCount = (total + MaxPayload - 1) / MaxPayload;

    for (u16 i = 0; i < partCount; i++)
    {
        u32 offset = i * MaxPayload;
        u32 size = total - offset;
        if (size > MaxPayload)
            size = MaxPayload;

        header->Magic = magic;
        header->FrameId = id;
        header->TotalSize = total;
        header->PartOffset = offset;
        header->PartIndex = i;
        header->PartCount = partCount;
        memcpy(packet + sizeof(PacketHeader), data + offset, size);

        ssize_t sent = sendto(Sock, packet, sizeof(PacketHeader) + size, 0, (sockaddr*)&Dest, sizeof(Dest));
        if (sent < 0)
        {
            StatSendErrors++;
            StatLastErrno = errno;
        }
        else
        {
            StatPacketsSent++;
        }
    }
}

static void Worker(void*)
{
    static u8 frame[FrameBytes];
    std::vector<u8> jpeg;
    jpeg.reserve(64 * 1024);

    while (true)
    {
        mutexLock(&Lock);
        while (!PendingValid && !Quit)
            condvarWait(&Cond, &Lock);
        if (Quit)
        {
            mutexUnlock(&Lock);
            break;
        }
        memcpy(frame, PendingFrame, FrameBytes);
        PendingValid = false;
        u32 id = PendingId;
        mutexUnlock(&Lock);

        if (Sock < 0)
            continue;

        u64 encodeStart = armGetSystemTick();
        bool sent = false;
        if (Config::StreamCodec != 0)
        {
            // lossless: drop the alpha channel, then QOI
            static u8 rgb[ScreenWidth * ScreenHeight * 3];
            for (int i = 0; i < ScreenWidth * ScreenHeight; i++)
            {
                rgb[i*3+0] = frame[i*4+0];
                rgb[i*3+1] = frame[i*4+1];
                rgb[i*3+2] = frame[i*4+2];
            }
            qoi_desc desc = { (unsigned)ScreenWidth, (unsigned)ScreenHeight, 3, QOI_SRGB };
            int len = 0;
            void* encoded = qoi_encode(rgb, &desc, &len);
            if (encoded)
            {
                // "Auto" (codec 2): 3D scenes compress badly losslessly and would flood
                // the wifi; above the budget the frame is sent as JPEG instead.
                bool tooBig = Config::StreamCodec == 2 && len > AutoLosslessMaxBytes;
                if (!tooBig)
                {
                    StatEncodeUs = armTicksToNs(armGetSystemTick() - encodeStart) / 1000;
                    SendFrame(id, (const u8*)encoded, len, MagicQoi);
                    StatFramesSent++;
                    StatLastBytes = len;
                    sent = true;
                }
                free(encoded);
            }
        }
        if (!sent)
        {
            int quality = Config::StreamQuality;
            if (quality < 10) quality = 10;
            if (quality > 100) quality = 100;

            jpeg.clear();
            stbi_write_jpg_to_func(WriteFunc, &jpeg, ScreenWidth, ScreenHeight, 4, frame, quality);
            StatEncodeUs = armTicksToNs(armGetSystemTick() - encodeStart) / 1000;

            SendFrame(id, jpeg.data(), jpeg.size(), MagicJpeg);
            StatFramesSent++;
            StatLastBytes = jpeg.size();
        }
    }
}

static void ApplyConfig()
{
    if (!Config::StreamEnable)
    {
        if (Sock >= 0)
            CloseSocket();
        return;
    }

    if (Sock < 0 || strcmp(CurHost, Config::StreamHost) != 0 || CurPort != Config::StreamPort)
    {
        strncpy(CurHost, Config::StreamHost, sizeof(CurHost) - 1);
        CurHost[sizeof(CurHost) - 1] = '\0';
        CurPort = Config::StreamPort;
        OpenSocket(CurHost, CurPort);
    }
}

// ---- discovery -------------------------------------------------------------

static void DiscoveryClose()
{
    if (DiscSock >= 0)
    {
        close(DiscSock);
        DiscSock = -1;
    }
}

static void DiscoveryOpen()
{
    DiscSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (DiscSock < 0)
        return;
    int one = 1;
    setsockopt(DiscSock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    fcntl(DiscSock, F_SETFL, fcntl(DiscSock, F_GETFL, 0) | O_NONBLOCK);
    DiscLastQuery = 0;
}

static void DiscoverySendTo(u32 addrNetworkOrder)
{
    DiscoveryQuery query = { DiscoveryQueryMagic, 1 };
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(DiscoveryPort);
    to.sin_addr.s_addr = addrNetworkOrder;
    sendto(DiscSock, &query, sizeof(query), 0, (sockaddr*)&to, sizeof(to));
}

static void DiscoverySendQuery()
{
    // limited broadcast, plus the subnet broadcast when the network config is known
    DiscoverySendTo(htonl(INADDR_BROADCAST));
    if (NifmReady)
    {
        u32 addr = 0, mask = 0, gateway, dns1, dns2;
        if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&addr, &mask, &gateway, &dns1, &dns2)) && addr && mask)
        {
            // nifm returns the addresses in network byte order
            u32 subnetBroadcast = addr | ~mask;
            if (subnetBroadcast != htonl(INADDR_BROADCAST))
                DiscoverySendTo(subnetBroadcast);
        }
    }
}

static void DiscoveryReceive()
{
    u8 buf[256];
    sockaddr_in from;
    while (true)
    {
        socklen_t fromLen = sizeof(from);
        ssize_t len = recvfrom(DiscSock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
        if (len < 0)
            break;
        if (len < (ssize_t)sizeof(DiscoveryReplyHeader))
            continue;
        DiscoveryReplyHeader* reply = (DiscoveryReplyHeader*)buf;
        if (reply->Magic != DiscoveryReplyMagic)
            continue;
        int nameLen = reply->NameLength;
        if (nameLen > len - (int)sizeof(DiscoveryReplyHeader))
            nameLen = len - sizeof(DiscoveryReplyHeader);
        if (nameLen > (int)sizeof(Device::Name) - 1)
            nameLen = sizeof(Device::Name) - 1;

        char host[16];
        strncpy(host, inet_ntoa(from.sin_addr), sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';

        int slot = -1;
        for (int i = 0; i < DeviceCount; i++)
            if (strcmp(Devices[i].Host, host) == 0)
                slot = i;
        if (slot < 0)
        {
            if (DeviceCount >= MaxDevices)
                continue;
            slot = DeviceCount++;
            strcpy(Devices[slot].Host, host);
        }
        memcpy(Devices[slot].Name, buf + sizeof(DiscoveryReplyHeader), nameLen);
        Devices[slot].Name[nameLen] = '\0';
        Devices[slot].LastSeen = NowNs();
    }
}

static void DiscoveryExpire()
{
    u64 now = NowNs();
    for (int i = 0; i < DeviceCount;)
    {
        if (now - Devices[i].LastSeen > 6000000000ULL)
        {
            Devices[i] = Devices[DeviceCount - 1];
            DeviceCount--;
        }
        else
        {
            i++;
        }
    }
}

void DiscoveryTick()
{
    if (DiscSock < 0)
        DiscoveryOpen();
    if (DiscSock < 0)
        return;

    u64 now = NowNs();
    DiscLastTick = now;
    if (now - DiscLastQuery > 1000000000ULL)
    {
        DiscoverySendQuery();
        DiscLastQuery = now;
    }
    DiscoveryReceive();
    DiscoveryExpire();
}

static void DiscoveryIdleCheck()
{
    // the list is not shown anymore: stop broadcasting
    if (DiscSock >= 0 && NowNs() - DiscLastTick > 3000000000ULL)
    {
        DiscoveryClose();
        DeviceCount = 0;
    }
}

int DiscoveredCount()
{
    return DeviceCount;
}

const Device& Discovered(int index)
{
    return Devices[index];
}

// ---- lifecycle -------------------------------------------------------------

void Init()
{
    mutexInit(&Lock);
    condvarInit(&Cond);
    Quit = false;

    NifmReady = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));

    // Core 2 keeps the encoder away from the emulation thread on core 0.
    Result rc = threadCreate(&WorkerThread, Worker, nullptr, nullptr, 1024 * 128, 0x2C, 2);
    if (R_SUCCEEDED(rc))
        rc = threadStart(&WorkerThread);
    WorkerRunning = R_SUCCEEDED(rc);
    if (!WorkerRunning)
        printf("Stream: failed to start worker thread (%08x)\n", rc);

    ApplyConfig();
}

void DeInit()
{
    if (WorkerRunning)
    {
        mutexLock(&Lock);
        Quit = true;
        condvarWakeAll(&Cond);
        mutexUnlock(&Lock);
        threadWaitForExit(&WorkerThread);
        threadClose(&WorkerThread);
        WorkerRunning = false;
    }
    CloseSocket();
    DiscoveryClose();
    if (NifmReady)
    {
        nifmExit();
        NifmReady = false;
    }
}

bool Enabled()
{
    ApplyConfig();
    DiscoveryIdleCheck();
    return Config::StreamEnable && Sock >= 0 && WorkerRunning;
}

void OnFrame(const u8* rgba)
{
    if (!Enabled())
        return;

    FrameCounter++;
    int skip = Config::StreamFrameSkip < 1 ? 1 : Config::StreamFrameSkip;
    if (FrameCounter % skip != 0)
        return;

    // Latest frame wins: if the encoder is still busy the previous pending
    // frame is simply replaced, which keeps latency low.
    mutexLock(&Lock);
    memcpy(PendingFrame, rgba, FrameBytes);
    PendingValid = true;
    PendingId++;
    condvarWakeOne(&Cond);
    mutexUnlock(&Lock);
}

bool AudioToTV()
{
    return Config::StreamAudio && Config::StreamEnable && Sock >= 0;
}

bool OnAudio(const s16* stereo, int frames, int sampleRate)
{
    if (!AudioToTV())
        return false;

    int sock = Sock;
    u8 packet[sizeof(AudioHeader) + AudioFramesPerPacket * 2 * sizeof(s16)];
    AudioHeader* header = (AudioHeader*)packet;
    while (frames > 0)
    {
        int n = frames > AudioFramesPerPacket ? AudioFramesPerPacket : frames;
        header->Magic = MagicAudio;
        header->Sequence = AudioSequence++;
        header->SampleRate = sampleRate;
        header->Channels = 2;
        header->Frames = n;
        memcpy(packet + sizeof(AudioHeader), stereo, n * 2 * sizeof(s16));
        if (sendto(sock, packet, sizeof(AudioHeader) + n * 2 * sizeof(s16), 0, (sockaddr*)&Dest, sizeof(Dest)) < 0)
        {
            StatSendErrors++;
            StatLastErrno = errno;
        }
        stereo += n * 2;
        frames -= n;
    }
    return true;
}

u32 FramesSent() { return StatFramesSent; }
u32 LastFrameBytes() { return StatLastBytes; }
u32 PacketsSent() { return StatPacketsSent; }
u32 SendErrors() { return StatSendErrors; }
int LastErrno() { return StatLastErrno; }
u32 EncodeMicros() { return StatEncodeUs; }

}
