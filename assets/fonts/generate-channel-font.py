"""Reproduce the fixed catalog-label font / 复现固定频道名称字库。"""
from pathlib import Path
import subprocess

fonts = Path(__file__).resolve().parent
catalog = (fonts.parents[1] / "main/passport_catalog.cpp").read_text(encoding="utf-8")
channels = catalog.split("kChannels[] = {", 1)[1].split("};", 1)[0]
symbols = "".join(sorted({c for c in channels + "机核电台" if ord(c) >= 128}))
subprocess.run([
    "node", "font-tools/node_modules/lv_font_conv/lv_font_conv.js",
    "--font", "NotoSansCJKsc-Bold.otf", "--symbols", symbols,
    "--range", "0x20-0x7E", "--size", "11", "--bpp", "2", "--format", "lvgl",
    "--no-compress", "--no-kerning", "--lv-font-name", "passport_channel_11",
    "--lv-include", "lvgl.h", "--output", "passport_channel_11.c",
], check=True, cwd=fonts)
