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

| Symbol | Face / size | Coverage | Line / baseline | Bitmap + large descriptors |
| --- | --- | --- | --- | --- |
| `passport_cjk_bold_22` | Black 22 px | Complete supported dynamic inventory | 32 / 8 px | 2,849,802 B |
| `passport_cjk_16` | Bold 16 px | Complete supported dynamic inventory | 24 / 6 px | 1,664,216 B |
| `passport_cjk_12` | Regular 12 px | Complete supported dynamic inventory | 17 / 4 px | 1,098,530 B |
| `passport_sans_12` | Regular 12 px | 95 printable ASCII characters | 14 / 3 px | 2,789 B |
| `passport_sans_14` | Regular 14 px | 95 printable ASCII characters | 17 / 4 px | 3,212 B |
| `passport_channel_11` | Bold 11 px | ASCII plus all five fixed catalog channel labels | 13 / 3 px | 3,832 B |
| `passport_hint_12` | Black 12 px | ASCII plus actual fixed button hints | 15 / 3 px | 6,792 B |

These estimates include 16-byte large glyph descriptors; maps and font metadata
add a small amount. The three complete fonts together use 5,612,548 bytes. Check the linked application against the 8 MiB device and actual
application partition in the firmware gate; do not infer fit from C-source sizes.
Fonts stay in read-only Flash and are never copied in full to RAM.

Every dynamic size includes all **20,976 basic CJK code points U+4E00–9FEF**,
ASCII, Latin-1, general/CJK punctuation and full-width forms supported by these
source faces. U+9FF0–9FFF, CJK extensions and emoji are outside the contract.
U+3031/U+3032 are excluded because their vertical form inflates line height.
There are currently 21,488 actual glyphs in each complete font. Dynamic programme
and chapter titles must use a complete font, never the fixed channel/hint subset.

## Reproduction and integration

Run `./generate-font.ps1` from this directory. It installs the lockfile-pinned
`lv_font_conv@1.5.3`, regenerates the complete and ASCII fonts, then invokes
`generate-channel-font.py`, `generate-hint-font.py` and `verify-font.py`.
Channel coverage is extracted from the actual `kChannels` table in
`main/passport_catalog.cpp`; hint coverage comes from `main/main.c`. Regenerate
after changing fixed labels. No live RSS data is used to shrink dynamic fonts.

Enable `CONFIG_LV_FONT_FMT_TXT_LARGE=y`, compile each selected font once, and
declare its matching symbol in the application. Any fallback uses an
application-owned writable `lv_font_t` descriptor; do not modify the generated
read-only descriptor. Keep the generated natural metrics. Two 22 px heading
lines need 64 px at default spacing; tighter UI spacing must be checked visually
for mixed-script overlap. The 12 px dynamic font requires a 17 px line pitch.

`python verify-font.py` checks the emitted inventory, required CJK and ASCII
boundaries, each glyph's vertical bounds, plain 2 bpp format, symbols, fixed text
coverage and a complete-font Flash budget. The UI validates UTF-8 and selected
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

## Retained pixel-font sources

`Monocraft.ttf`, `Monocraft-OFL.txt`, `passport_pixel_14.c`,
`passport_pixel_20.c` and `generate-pixel-font.ps1` are retained as optional design
resources. They are not linked by the current sans-serif UI. Monocraft is by
Idrees Hassan, copyright 2022, under SIL OFL 1.1. Its pinned
[upstream source](https://github.com/IdreesInc/Monocraft/blob/50e2d1c5405e9ca19c6c697d3aad0ded5a44aef9/dist/Monocraft-ttf/Monocraft.ttf)
has SHA-256 `262e31822e0a75be567b02320bfa092143d871e8a273e529d1c5ab0c04cd215f`.
