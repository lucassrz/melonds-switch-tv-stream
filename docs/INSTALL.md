# Installation guide

Fifteen minutes, two devices. You need:

- A Nintendo Switch running custom firmware (Atmosphère) that can launch homebrew, with wifi.
- An Android TV device on the same wifi network: Google TV, Nvidia Shield, Chromecast with
  Google TV, or a smart TV running Android TV 8 or newer.
- Your own DS dumps: `bios7.bin`, `bios9.bin`, `firmware.bin` from a DS you own, and your
  game files (`.nds`).

## 1. The Switch side

1. Download `melonDS-switch-<version>.nro` from the
   [Releases](https://github.com/lucassrz/melonds-switch-tv-stream/releases) page.
2. On the SD card, create `switch/melonds/` and copy into it:
   - `melonDS.nro` (rename the downloaded file to this)
   - `bios7.bin`, `bios9.bin`, `firmware.bin`
3. Put your games anywhere on the card, for example `roms/nds/`.
4. Launch melonDS from the homebrew menu. Open **Browse files** once and enter your games
   folder: the home screen then lists its games with their icons.

Already have melonDS installed? Replace `melonDS.nro` only. Your `melonDS.ini` is kept.
Check **Emulation** afterwards: the JIT recompiler on and the CPU clock at 1785 MHz are
the defaults of this build and are required for 3D games to run at full speed.

## 2. The TV side

1. Download `melonDS-tv-<version>.apk` from the Releases page.
2. Install it on the TV. Two common ways:
   - **USB stick**: copy the APK to a stick, open it on the TV with a file manager app
     (allow "unknown sources" for that app when asked).
   - **adb** from a computer, after enabling developer options and network debugging on the
     TV: `adb connect <TV address>` then `adb install melonDS-tv-<version>.apk`.
3. Open **melonDS TV**. It shows the TV's name and waits for the Switch.

## 3. Link them

1. On the Switch, open melonDS and choose **TV streaming** in the sidebar.
2. Turn on **Stream top screen to a TV over wifi**.
3. In **TVs found**, pick your TV. It appears within a few seconds while the TV app is open.
   If it never shows up, type the address displayed by the TV app in **TV address**.
4. Choose **Audio: TV** if you want the sound on the TV.
5. Go back and start a game, or press **X** on a library tile to start it with the stream.

The Switch now shows only the bottom screen; the top screen and the touch controls work as
on a real DS. The sidebar card at the bottom shows which TV is linked.

## On the TV, with the remote

- **OK** or **Menu**: settings (display mode, audio, audio latency, statistics overlay).
- **Back**: closes the settings.
- Display "Fill screen (sharp)" is the default: a clean integer enlargement that fills the
  height. "Pixel perfect" keeps exact pixels with black bars, "Fill screen (smooth)" blurs.

## Troubleshooting

- **The TV is not in the list.** Both devices must be on the same wifi network, and some
  routers block device-to-device traffic ("AP isolation" or "guest network"). Typing the
  address in **TV address** works around discovery; the TV app shows its address.
- **The picture stutters.** Move closer to the router or switch to 5 GHz. On the Switch,
  set **Image** to JPEG and **Send** to 30 fps to halve the bandwidth. On the TV, raise
  **Audio latency** if the sound crackles.
- **Games are slow even without streaming.** Settings > Emulation: JIT on, CPU clock
  1785 MHz.
- **A game takes a minute to load.** The file is fragmented on the SD card. Copy it again
  from a computer in one go, or reformat the card as FAT32 with 64 KB clusters.
- **No sound on the TV.** Audio is on the Switch by default. Set **Audio: TV** on the Switch
  and check that audio is on in the TV app settings.
