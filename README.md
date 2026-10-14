**English** · [简体中文](README.zh_CN.md)

# Gcores Passport Player

A pocket Gcores podcast player for FoloToy AI Passport, with five channels, Gcores FM, text timelines and saved playback progress.

![Player UI render](assets/gcores/community/cover.png)

This image is a native UI render with illustrative episode text, not a device capture. This is an independent application, not an official Gcores product.

## Features

- Browse up to 100 recent episodes in each of five channels: gaming, life, interests, Heshi Qitan and Yinpingxi Roaming Guide. Local paging keeps RAM bounded.
- Play or pause, adjust volume, preview a seek target while holding a button and seek on release.
- Read the current and upcoming chapter text directly in the player; overflowing text scrolls automatically.
- Reopen the playing episode without restarting it. A bounded per-episode history saves progress periodically for returning after switching programs or restarting.
- Listen with the screen off, set a stop timer, adjust brightness and configure automatic screen-off.
- Listen to Gcores FM with current/upcoming programme titles and lightweight radio animation.
- Save one or two Wi-Fi networks through the temporary hotspot management page; automatically reconnect to an available saved network.

## Quick start

1. On first startup, connect your phone to `Gcores-Passport` using the password on the device. Open `http://192.168.4.1`, enter one or two 2.4 GHz Wi-Fi configurations (the second is optional), and tap Save and connect. The device reports a connected network and closes its hotspot when successful.
2. On the home screen, press Up/Down to select an episode; hold Up/Down to switch channels left/right. Press OK to listen.
3. In the player, Up/Down changes volume, holding Up/Down previews backward/forward positioning, and release commits the seek. OK toggles play/pause; holding OK returns home.
4. Hold OK on the home screen to enter Settings, where Gcores FM and Return to current playback are available. Double-press OK to blank the screen while listening; the first function-button press only wakes it.

For retrying setup, changing networks and all page controls, see the [user guide](docs/development/gcores-player.md).

## Requirements and installation

The target is an ESP32-C3 AI Passport with 8 MB Flash, no PSRAM and a 240 × 320 portrait display. Build with ESP-IDF **5.5.3**, Git, Python and a host C/C++ compiler. Windows validation also requires Git for Windows Bash and PowerShell 7.

```bash
git clone https://github.com/sonicpic/gcores-passport-player.git
cd gcores-passport-player
# Activate ESP-IDF 5.5.3 in this terminal first.
idf.py --version
./tools/validate.sh --all
```

Windows installations can use `tools/develop.ps1` with explicit `-IdfRoot` and `-ToolsRoot` paths. See [environment setup](docs/development/engineering/environment-setup.md) and [build validation](docs/development/gcores-validation.md).

The verified merged image is `build/FoloToy-AI-Passport-full.bin`, installed at `0x0`. A merged installation can reset Wi-Fi settings and saved progress. The application-only offset is `0x30000`; use it only with a verified compatible partition table. Never flash an application-only image at `0x0`. See the [installation contract](docs/development/gcores-player.md#firmware-installation).

## Documentation

- [User guide and settings](docs/development/gcores-player.md)
- [Streaming, supported formats and seeking](docs/development/gcores-audio.md)
- [Memory budget and resource lifecycle](docs/development/gcores-memory.md)
- [Validation and device acceptance](docs/development/gcores-validation.md)
- [Source attribution and licenses](docs/development/gcores-sources.md)
- [Full documentation index](docs/README.md)

## License

Application source is [GPL-3.0-only](LICENSE), derived from [gcores-pocket-player](https://github.com/sonicpic/gcores-pocket-player). The AI Passport BSP retains its MIT notices, fonts retain SIL OFL notices, and the vendored codec retains its own license. Artwork does not imply Gcores endorsement. See the [third-party inventory](docs/development/gcores-sources.md).
