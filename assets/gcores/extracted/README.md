<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Original reference crops

The player cover, robot, waveform decorations are extracted directly from
the user-supplied `../references/style-02-player.png` (1086 × 1448). They are not
generated variations or redrawn substitutes. The source SHA-256 is
`816cdb0fa38f0eea333e42b756922af7b6065b6c16176e598204d04fec7f33e8`.

The list badge is cropped separately from `../references/style-10-list-icon.png`.

`*-source.png` preserves each exact crop at the original resolution. The device
PNGs apply deterministic area downsampling with Pillow `BOX` (the list badge uses `LANCZOS`), then the generated
C file stores RGB565 pixels in constant Flash data. No PNG decoder, full-screen
framebuffer or generated alpha mask is needed. Total raw pixel data is 31,596
bytes; LVGL image widgets/decoder descriptors still consume the measured UI pool.

| Asset | Source crop (left, top, right, bottom) | Device pixels |
| --- | --- | --- |
| Player cover | 73, 287, 407, 681 | 82 × 105 |
| Robot | 689, 629, 1034, 881 | 74 × 54 |
| Waveform decoration | 299, 750, 700, 881 | 86 × 28 |
| List badge (`style-10-list-icon.png`) | 125, 128, 1128, 1131 | 28 × 28 |

Reproduce from the repository root with `python assets/gcores/extract-reference-assets.py`.
The provided opaque background is retained in each crop. The waveform is static
decoration; the separate volume blocks use the real application volume. These
images are user-provided design material, not an official logo package or a new
grant of third-party licensing rights.
