**English** · [简体中文](gcores-player.zh_CN.md)

# User guide

The application has three main pages: podcast list (home), player and Settings.
Five home tabs correspond to the public Gcores gaming, life, interests, Heshi
Qitan and Yinpingxi Roaming Guide feeds. Each keeps up to 100 recent episodes
in Flash; scrolling reads a four-record RAM window. If the currently playing
episode ages out, it remains available as a normal episode row.
Up, Down and OK are programmable function buttons; the separate power button
is a hardware control, not an application input.

## Wi-Fi setup

With no saved network, startup opens the setup page. Connect a phone to
`Gcores-Passport` with the password displayed on the device, then open
`http://192.168.4.1`. Enter the 2.4 GHz network name and password, and tap Save
and connect. WPA/WPA2 and open networks are supported; leave the password empty
for a new open network. Enterprise Wi-Fi is not supported.

Successful connection displays the connected state and automatically closes
the setup hotspot. An uncompleted setup expires after ten minutes. To retry,
hold Down on the setup page to reopen the hotspot, reconnect the phone and
submit corrected details. To change networks, hold OK on home, select Wi-Fi
setup in Settings, then press OK. Leaving manual setup closes the hotspot.
Up to two networks are remembered; the second is optional. The hotspot management page accepts one or two networks in one submission. Chinese SSIDs are supported in UTF-8, within Wi-Fi's
32-byte limit (usually up to ten Chinese characters). Valid configurations are saved before connecting, so a second network can be
remembered while it is unavailable. Saving one network adds or updates it and
preserves the other; submitting two saves both together. Re-entering the same
SSID updates its password; leave the password empty to retain the saved one,
or select the open-network checkbox to remove it. A third network saved alone
replaces the least recently used one.

Startup tries the last successful network first, then the other saved network.
With saved networks unavailable, the device stays offline and uses increasing
retry intervals without opening a setup hotspot. It does not periodically scan
while connected. Network credentials are stored on the device and are not embedded in firmware.
NVS credentials are not encrypted by this configuration.

The list uses cached data immediately. Once Wi-Fi and internet time are ready,
automatic refresh updates all five channels at most once per Beijing calendar
day, while audio is inactive. The last fully successful refresh date persists
across restarts; an unknown clock defers automatic refresh. A failed automatic
attempt is not repeated continuously, and Settings can retry at any time.
Settings also updates all five channels sequentially. Each channel commits independently;
a failure retains that channel's old list without preventing other updates.
Local paging does not interrupt playback or use the network. Explicit refresh
or chapter fetching may stop and restore playback to avoid overlapping TLS and
decoder allocations.

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
preserves position and pause state. Settings shows a highlighted Return to
current playback item first after a playback session starts; opening it preserves
the session. A saved record alone does not show this item on a fresh boot.

During a player hold, accelerated steps move only the preview target. Release
commits one seek. The selected position remains visible until that request
completes; a paused seek stays paused until explicit resume. Range-based MP3
positioning is approximate, and fallback positioning can take longer.

Progress is saved every minute, when replacing the playing episode and before
deep sleep; completion is recorded as well. Selecting a different episode uses
its saved position automatically. Completed episodes start from the beginning.
History uses channel plus GUID rather than list position or enclosure URL, so
RSS reordering and URL changes do not lose the association. The most recently
saved 32 episodes are retained; a changed save evicts the oldest record when
full. Unexpected power loss may discard progress since the last successful save.

Double-press OK on any page blanks the screen without stopping playback. The
first function-button gesture wakes the screen and is consumed through release.
Selected brightness is restored on wake. The hardware power key is independent.

## Player and settings

The player displays the title, channel, current and next chapter text, progress,
elapsed time and duration. Long titles/list rows/chapter labels scroll; scrolling
stops while the screen is off. When no timeline is available, both labels show
the unavailable state and their timestamps are hidden. A loading state is shown
without exposing internal fetch steps.

Settings starts with Return to current playback when available, then Gcores FM.
These two rows use aligned artwork icons; the remaining rows are text-only.
It also offers refresh, Wi-Fi setup, brightness, automatic screen-off, timed
stop, immediate screen-off, stop-and-deep-sleep and button help.

Select Gcores FM to open the player and join the official synchronized station.
Up/down clicks change volume; OK pauses, and OK again rejoins the current station
position. Hold OK to return to Home. Seeking, episode timeline, progress bar and
per-episode progress saving are absent in station mode. The large title shows the current programme; the former timeline area shows a scrolling next-programme title and lightweight decorative radio bars. Screen-off playback and
timed stop remain available. Entering FM saves the previous podcast position.

Episode timeline fetching retries a transient failure once before audio starts.
Returning to an active episode with an unavailable cache retries the timeline
without resetting its playback position or pause intent. A timeline whose first
chapter begins after zero shows its first entries rather than reporting absence.

- Brightness: 20/40/60/70/80/100%.
- Automatic screen-off: 15/30/60/120/300 seconds or always on.
- Timed stop: off or 15/30/45/60 minutes.
- Screen-off keeps playing. Stop-and-deep-sleep saves progress and stops audio;
  a function key wakes deep sleep through a reboot.

The status bar uses graphical volume/battery fill. Battery unavailable is
represented explicitly. There is no controllable software power key or rear
indicator LED in the published board interface.
Home and Settings have a pure-black body while retaining shared header/footer artwork.

## Firmware installation

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | `0x26000` |
| PHY | `0x2F000` | `0x1000` |
| Factory application | `0x30000` | `0x4D0000` |
| Catalog cache | `0x500000` | `0x300000` |

The layout fits 8 MB and has no OTA slot. A verified merged image belongs at
`0x0`; it can reset stored settings and progress. Application-only updates belong
at `0x30000` and require a verified matching partition table. No routine
whole-chip erase is needed. The first upgrade from the former larger application
partition must write both the matching partition table at `0x8000` and the smaller
application at `0x30000`. NVS and PHY retain their offsets and sizes; segmented
writes preserve settings and the last playback snapshot. Legacy five-episode
NVS catalogs are imported into the Flash cache. Identify the actual device port before flashing.
See [build and validation](gcores-validation.md) and the
[flashing policy](engineering/firmware-layout.md).

The player streams from the network; SD/local-library playback is not supported.
See [supported audio and positioning limits](gcores-audio.md).
