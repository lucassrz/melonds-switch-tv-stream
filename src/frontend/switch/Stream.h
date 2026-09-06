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
#ifndef STREAM_H
#define STREAM_H

#include "types.h"

// Streams the emulated top screen over the network (JPEG frames over UDP)
// so it can be shown on a TV while the Switch displays the bottom screen.
namespace Stream
{

void Init();
void DeInit();

// True when streaming is enabled in the config and a destination is set up.
bool Enabled();

// Called from the render thread once per presented frame with the top screen
// as tightly packed RGBA8, 256x192. Copies the data and returns immediately.
void OnFrame(const u8* rgba);

// Audio: called from the audio thread with interleaved stereo s16 samples.
// Returns true when the samples were sent to the TV and the Switch should
// stay silent.
bool OnAudio(const s16* stereo, int frames, int sampleRate);
bool AudioToTV();

// Statistics for the on-screen display.
u32 FramesSent();
u32 LastFrameBytes();
u32 PacketsSent();
u32 SendErrors();
int LastErrno();
u32 EncodeMicros();

// Discovery of receivers on the local network. Call DiscoveryTick() every
// frame while the list is shown; it broadcasts a query once a second and
// collects the replies. The socket is closed automatically a few seconds
// after the last tick.
struct Device
{
    char Host[16];
    char Name[64];
    u64 LastSeen;
};
void DiscoveryTick();
int DiscoveredCount();
const Device& Discovered(int index);

}

#endif
