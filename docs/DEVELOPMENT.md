# Development

## Repository layout

| Folder        | Content                                                                 |
|---------------|-------------------------------------------------------------------------|
| `switch/`     | melonDS Switch, based on [Gheovgos/melonDS](https://github.com/Gheovgos/melonDS) `switch-new`, with the streaming module and the new interface (upstream history kept) |
| `android-tv/` | melonDS TV, the Android TV app (Kotlin, no external dependencies)       |
| `tools/`      | `stream_receiver.py`, a desktop test receiver                           |
| `docs/`       | This documentation and the screenshots                                  |

## Building the Switch homebrew

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `switch-dev` and
`switch-curl` packages, plus CMake and Ninja.

```bash
export DEVKITPRO=/opt/devkitpro PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitA64/bin:$PATH
cd switch && mkdir -p build && cd build
cmake .. -G Ninja -DENABLE_OGLRENDERER=OFF -DBUILD_QT_SDL=OFF \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-cross-Switch.cmake -DCMAKE_BUILD_TYPE=Release
ninja
```

To try a build without copying it to the SD card, open the homebrew menu on the Switch,
press Y (netloader) and run `nxlink -a <switch address> -s build/melonDS.nro`. The
`-s` flag streams the console output back, which is the only debug channel.

## Building the Android TV app

Requires the Android SDK (platform 37, build-tools 36) and a JDK 17 or later.

```bash
cd android-tv
echo "sdk.dir=$HOME/Library/Android/sdk" > local.properties
./gradlew assembleDebug
adb connect <TV address> && adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## Desktop test receiver

`tools/stream_receiver.py` shows the stream in a window on a computer (Python 3, Pillow,
Tk) and answers discovery like the TV app does, so the Switch lists it as a TV.

```bash
python3 -m venv tools/.venv && tools/.venv/bin/pip install pillow
tools/.venv/bin/python tools/stream_receiver.py
```

## Continuous integration and releases

`.github/workflows/build.yml` builds both binaries on every push. Pushing a `v*` tag creates
a GitHub release with `melonDS-switch-<tag>.nro` and `melonDS-tv-<tag>.apk` attached.

## How the stream works

The deko3d renderer of melonDS Switch composes the top screen on the GPU. After each frame
it is copied back to CPU memory, handed to a worker thread on a spare core, encoded (QOI
lossless or JPEG) and sent as UDP datagrams. Audio samples are sent as raw PCM from the
audio thread. The TV app reassembles frames, keeps the newest complete one and drops
incomplete ones, so a lost packet costs one frame and never a freeze.

### Wire format

Video, one datagram = 20-byte little-endian header + up to 1400 bytes of the image file:

| Field      | Type | Meaning                                                         |
|------------|------|-----------------------------------------------------------------|
| magic      | u32  | `0x3153444D` (`"MDS1"`, JPEG) or `0x3253444D` (`"MDS2"`, QOI)   |
| frameId    | u32  | increases with every frame                                      |
| totalSize  | u32  | size of the whole image file                                    |
| partOffset | u32  | byte offset of this payload in the file                         |
| partIndex  | u16  | part number                                                     |
| partCount  | u16  | number of parts of this frame                                   |

The image is 256x192.

Audio: `u32 0x4153444D ("MDSA"), u32 sequence, u32 sampleRate, u16 channels, u16 frames`,
then interleaved s16 PCM.

Discovery (UDP 9798): the Switch broadcasts `u32 0x5153444D ("MDSQ"), u32 version`;
receivers answer `u32 0x5253444D ("MDSR"), u16 streamPort, u16 nameLength, UTF-8 name`.

## Switch-side settings (`melonDS.ini`)

| Key               | Default | Meaning                                                    |
|-------------------|---------|------------------------------------------------------------|
| `StreamEnable`    | 0       | Stream the top screen                                      |
| `StreamHost`      |         | TV address (set from the "TVs found" list or typed)        |
| `StreamHostName`  |         | Name of the TV picked from the list, shown in the sidebar  |
| `StreamPort`      | 9797    | UDP port of the TV app                                     |
| `StreamCodec`     | 2       | 0 JPEG, 1 lossless (QOI), 2 auto (lossless if small)       |
| `StreamQuality`   | 92      | JPEG quality (90+ keeps colors sharp)                      |
| `StreamFrameSkip` | 1       | 1 = 60 fps, 2 = 30 fps, 3 = 20 fps                         |
| `StreamAudio`     | 0       | 0 Switch speakers, 1 TV                                    |
| `StreamHideTop`   | 1       | Show only the bottom screen on the Switch while streaming  |

`ShowPerformanceMetrics=1` adds a "stream:" line to the on-screen metrics (frames, packets,
send errors, encoded size, encode time).

## Interface

The Switch interface is an immediate-mode GUI (`BoxGui`) drawn with deko3d. Colors and
metrics live in `src/frontend/switch/Style.h`; rounded corners and outlines come from the
default fragment shader; touch is handled in `BoxGui` (tap = select and confirm, drag =
scroll). Any interactive element must be registered with `BoxGui::InputElement` so both the
controller and the touch screen reach it.
