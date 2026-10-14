**English** · [简体中文](gcores-validation.zh_CN.md)

# Build and acceptance

## Reproducible gate

Activate ESP-IDF 5.5.3 and make a host C/C++ compiler available. Run from the
repository root:

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh --all
```

The static gate checks documentation/languages/links, repository safety and host
regressions. It covers catalog/cache/RSS/timeline, navigation, progress saving,
Wi-Fi rollback, audio/HTTP/range logic and BSP failures. Firmware validation
builds a fresh isolated configuration from `sdkconfig.defaults`, verifies the
partition layout and archives a matching merged image, application, ELF and MAP.

The output is `build/FoloToy-AI-Passport-full.bin`; the content-addressed debug
bundle is under `build/firmware/<full-image-sha256>/`. Verify before use:

```bash
python tools/archive_firmware.py verify build/firmware/<full-image-sha256>
```

Keep generated binaries and raw device logs outside Git. A failed build does not
validate an older output still present in `build/`. Decode a crash only with the
ELF belonging to the installed image. See the [build guide](engineering/build-and-test.md).

## Native UI checks

The native renderer compiles actual UI code/fonts with LVGL and a 32 KiB pool.
It covers all main/subpages, channel colors, volume/battery fill, complete long
strings, scrolling across unchanged updates, screen-off/wake, feedback lifetime,
repeated page teardown and memory integrity. Fixtures use synthetic display data.
Host rendering does not verify LCD transfer, physical color, button timing or sound.
See [render setup](../../assets/gcores/README.md).

## On-device acceptance

1. Identify the target device, verified artifact and intended write offset.
   Preserve NVS/PHY only through a verified compatible application-only update.
2. Check startup, the current UI, longest titles, all three keys, channel changes,
   settings, help and returning to the playing episode without restart.
3. Test first setup, bad credentials, retry, changing networks, setup exit and
   automatic hotspot shutdown. Verify time synchronization before HTTPS loading.
4. Test playback, pause/resume, forward/backward seek, unsupported media,
   network interruption, progress saving/restart and repeated stream replacement.
5. Exercise screen-off playback, wake consumption, timers and deep sleep.
   Inspect heap/largest-block/stack watermarks and observe a prolonged session.
6. Measure current on battery under comparable volume/media/network conditions
   if claiming power savings; USB console operation alone is insufficient.

Report Build, Host tests, Device tests and Unverified separately. Compile success
or a flash acknowledgement is not physical acceptance. All-format coverage,
worst-case network behavior, long soak and current measurements need explicit
evidence. Local test journals belong in the ignored engineering notebook.
