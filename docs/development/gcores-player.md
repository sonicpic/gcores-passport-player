**English** · [简体中文](gcores-player.zh_CN.md)

# User guide

The application has three main pages: podcast list (home), player and Settings.
Five home tabs correspond to the public Gcores gaming, life, interests, Heshi
Qitan and Yinpingxi Roaming Guide feeds. Each keeps the latest five episodes.
Up, Down and OK are programmable function buttons; the separate power button
is a hardware control, not an application input.

## Wi-Fi setup

With no saved network, startup opens the setup page. Connect a phone to
`Gcores-Passport` with the password displayed on the device, then open
`http://192.168.4.1`. Enter the 2.4 GHz network name and password, and tap Save
and connect. WPA/WPA2 and open networks are supported; leave the password empty
only for an open network. Enterprise Wi-Fi is not supported.

Successful connection displays the connected state and automatically closes
the setup hotspot. An uncompleted setup expires after ten minutes. To retry,
hold Down on the setup page to reopen the hotspot, reconnect the phone and
submit corrected details. To change networks, hold OK on home, select Wi-Fi
setup in Settings, then press OK. Leaving manual setup closes the hotspot.
Network credentials are stored on the device and are not embedded in firmware.
NVS credentials are not encrypted by this configuration.

The list uses cached data immediately. An empty channel is fetched after Wi-Fi
and internet time are ready; Settings can refresh the selected channel. A failed
refresh preserves the existing list. Refreshing or fetching chapters may briefly
stop and restore playback to avoid overlapping network and decoder allocations.

## Buttons

| Page | Press Up / Down | Hold Up / Down | Press OK | Hold OK |
| --- | --- | --- | --- | --- |
| Home | Select an episode | Switch channel left / right | Play or reopen episode | Settings |
| Player | Increase / decrease volume | Preview backward / forward seek | Play / pause | Home |
| Settings | Select an item | No additional action | Adjust or execute | Home |
| Wi-Fi setup | Follow on-screen help | Down reopens setup hotspot | Return and close hotspot | Return |
| Button help | Read the page | No additional action | Follow on-screen help | Return |

A hold starts after approximately 0.5 seconds. Home tab selection wraps at the
ends; each hold switches once. Browsing another channel does not stop playback.
The currently playing or paused episode is marked in the list. Opening it
preserves position and pause state; there is no separate return-to-player row.

During a player hold, accelerated steps move only the preview target. Release
commits one seek. The selected position remains visible until that request
completes; a paused seek stays paused until explicit resume. Range-based MP3
positioning is approximate, and fallback positioning can take longer.

Double-press OK on any page blanks the screen without stopping playback. The
first function-button gesture wakes the screen and is consumed through release.
Selected brightness is restored on wake. The hardware power key is independent.

## Player and settings

The player displays the title, channel, current and next chapter text, progress,
elapsed time and duration. Long titles/list rows/chapter labels scroll; scrolling
stops while the screen is off. When no timeline is available, both labels show
the unavailable state and their timestamps are hidden. A loading state is shown
without exposing internal fetch steps.

Settings offers refresh, Wi-Fi setup, brightness, automatic screen-off, timed
stop, immediate screen-off, stop-and-deep-sleep and button help.

- Brightness: 20/40/60/70/80/100%.
- Automatic screen-off: 15/30/60/120/300 seconds or always on.
- Timed stop: off or 15/30/45/60 minutes.
- Screen-off keeps playing. Stop-and-deep-sleep saves progress and stops audio;
  a function key wakes deep sleep through a reboot.

The status bar uses graphical volume/battery fill. Battery unavailable is
represented explicitly. There is no controllable software power key or rear
indicator LED in the published board interface.

## Firmware installation

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | `0x26000` |
| PHY | `0x2F000` | `0x1000` |
| Factory application | `0x30000` | `0x7D0000` |

The layout fits 8 MB and has no OTA slot. A verified merged image belongs at
`0x0`; it can reset stored settings and progress. Application-only updates belong
at `0x30000` and require a verified matching partition table. No routine
whole-chip erase is needed. Identify the actual device port before flashing.
See [build and validation](gcores-validation.md) and the
[flashing policy](engineering/firmware-layout.md).

The player streams from the network; SD/local-library playback is not supported.
See [supported audio and positioning limits](gcores-audio.md).
