# melonDS Switch TV Stream

**Nintendo DS games on a modded Switch, with the top screen on your Android TV over wifi
and the touch screen in your hands. No dock, no cable.**

![Home screen](docs/screenshots/home.jpg)

A patched [melonDS](https://github.com/melonDS-emu/melonDS) for the Switch sends the top
screen (and optionally the audio) to a small Android TV app on the same wifi network. The
Switch keeps the bottom screen full size. Both parts are in this repository.

- **Setup**: [docs/INSTALL.md](docs/INSTALL.md)
- **Downloads**: [Releases](https://github.com/lucassrz/melonds-switch-tv-stream/releases)
- **Building and protocol details**: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)

## Highlights

- A game library with icons, a "continue playing" card, touch-friendly menus.
- Lossless picture for 2D, high-quality JPEG for 3D, chosen automatically.
- Audio on the TV or on the Switch. The Switch finds the TV by itself.
- Everything stays on your local network. Nothing is collected.

You need your own DS BIOS and firmware dumps and your own game dumps. None are included.

## Credits and license

melonDS is the work of Arisotura and the melonDS team; the Switch port was created by
Hydr8gon and carried on by RSDuck, Jpe230 and Gheovgos. See [THIRD_PARTY.md](THIRD_PARTY.md).
GPL-3.0-or-later, like melonDS. Unofficial project, not affiliated with Nintendo, Google
or the melonDS team.
