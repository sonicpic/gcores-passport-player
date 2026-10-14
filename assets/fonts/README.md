<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Gcores Passport font assets

The player uses the proportional Noto Sans CJK SC family with antialiased 2 bpp,
uncompressed LVGL bitmaps. Dynamic titles use a real Black face at 22 px; body
text uses Bold at 16 px and chapter labels use Regular at 12 px.

## Sources and license

Noto Sans CJK SC 2.004 is copyright 2014–2021 Adobe and redistributed under the
[SIL Open Font License](OFL.txt). All three source fonts and renamed derivatives are
stored here. Sources are from [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk/tree/main/Sans/OTF/SimplifiedChinese).

| Source | SHA-256 |
| --- | --- |
| `NotoSansCJKsc-Regular.otf` | `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b` |
| `NotoSansCJKsc-Bold.otf` | `b5f0d1a190a7f9b43c310a8850630af12553df32c4c050543f9059732d9b4c0a` |
| `NotoSansCJKsc-Black.otf` | `2267c4a0312d267dff8c0d1609948f7d949a3da8be3e126f5e2690cb9cc883b4` |

## Generated fonts and budget

Dynamic Chinese text supports exactly the **8,105 characters of the 2013 Table
of General Standard Chinese Characters**, plus the unchanged 512 supported
ASCII, Latin-1, punctuation and full-width glyphs from the existing contract.
This is a product character-set contract, not a subset of the current RSS feed.
The canonical inventory, three levels, pinned provenance and checksums are in
[standard-characters](standard-characters/README.md).

| Symbol | Face / size | Actual glyphs | Line / baseline | Bitmap + large descriptors |
| --- | --- | --- | --- | --- |
| `passport_cjk_bold_22` | Black 22 px | 8,617 | 32 / 8 px | 1,121,087 B |
| `passport_cjk_16` | Bold 16 px | 8,617 | 24 / 6 px | 655,798 B |
| `passport_cjk_12` | Regular 12 px | 8,617 | 17 / 4 px | 433,176 B |
| `passport_sans_12` | Regular 12 px | 95 ASCII | 14 / 3 px | 2,789 B |
| `passport_sans_14` | Regular 14 px | 95 ASCII | 17 / 4 px | 3,212 B |
| `passport_channel_11` | Bold 11 px | Fixed catalog labels | 13 / 3 px | 3,832 B |
| `passport_hint_12` | Black 12 px | Fixed button hints | 15 / 3 px | 6,792 B |

The three dynamic fonts use 2,210,061 bytes of bitmap and 16-byte large glyph
descriptors, saving 3,402,487 bytes against the former 20,976-basic-CJK inventory
at the same weights and sizes. Maps and metadata add target-dependent overhead;
check the actual linked application in the firmware gate. Fonts remain in
read-only Flash and are not copied in full to RAM.

The standard list includes 273 characters outside the previous basic-CJK range,
including 196 supplementary-plane characters. All 8,105 have been checked in
all three source faces and actual generated inventories. Traditional or rare
characters outside this exact list, CJK extensions outside the list and emoji
are unsupported; the UI substitutes visible `?` instead of silently omitting
text. U+9F98 and emoji are negative probes; U+2C27C is supported because it is
in the standard list. No programme titles are baked into images.

## Reproduction and integration

Run `./generate-font.ps1` from this directory. It installs the lockfile-pinned
`lv_font_conv@1.5.3`, runs `generate-standard-fonts.js` for the dynamic fonts and regenerates ASCII fonts, then invokes
`generate-channel-font.py`, `generate-hint-font.py` and `verify-font.py`.
Channel coverage is extracted from the actual `kChannels` table in
`main/passport_catalog.cpp`; hint coverage comes from `main/main.c`. Regenerate
after changing fixed labels. No live RSS data is used to shrink dynamic fonts. The Node wrapper loads
8,105 symbols from the checked-in manifest in-process, avoiding the Windows
command-line length limit. It rejects a converter other than 1.5.3, source-list
hash/count changes and missing source glyphs before conversion.

Enable `CONFIG_LV_FONT_FMT_TXT_LARGE=y`, compile each selected font once, and
declare its matching symbol in the application. Any fallback uses an
application-owned writable `lv_font_t` descriptor; do not modify the generated
read-only descriptor. Keep the generated natural metrics. Two 22 px heading
lines need 64 px at default spacing; tighter UI spacing must be checked visually
for mixed-script overlap. The 12 px dynamic font requires a 17 px line pitch.

`python verify-font.py` checks the emitted inventory, required CJK and ASCII
boundaries, each glyph's vertical bounds, plain 2 bpp format, symbols, fixed text
coverage and a dynamic-font Flash budget. The UI validates UTF-8 and selected
glyph availability and replaces unsupported code points with visible `?` while
logging their U+ value. Neither compilation nor coverage auditing proves bitmap
decoding or device appearance; inspect actual LVGL rendering and the device.

The public symbol `passport_cjk_bold_22` is retained for source compatibility;
its actual face is Black. No synthetic outline or baked programme title is used.
The fixed footer uses Black 12 px (15 px line pitch) and fixed channel names use
Bold 11 px. The complete body font requires 24 px line pitch. The
[weight comparison](font-weight-comparison.png) enlarges 240 px samples 3×;
regenerate it with `python generate-weight-preview.py` (Pillow required). This
source-font preview is supplementary; actual LVGL and device checks are required.
