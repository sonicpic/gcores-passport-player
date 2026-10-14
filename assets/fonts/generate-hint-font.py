"""Reproduce the fixed button-hint subset / 复现固定按键提示字库。"""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
main = (root / "main/main.c").read_text(encoding="utf-8")
hints = "".join(line for line in main.splitlines() if re.search(r"\bhint\s*=", line))
symbols = "".join(sorted({c for c in hints + "音量进度播放暂停菜单选择栏目打开设置调整返回确认浏览" if ord(c) >= 128}))
fonts = Path(__file__).resolve().parent
subprocess.run([
    "node", "font-tools/node_modules/lv_font_conv/lv_font_conv.js",
    "--font", "NotoSansCJKsc-Black.otf", "--symbols", symbols,
    "--range", "0x20-0x7E", "--size", "12", "--bpp", "2", "--format", "lvgl",
    "--no-compress", "--no-kerning", "--lv-font-name", "passport_hint_12",
    "--lv-include", "lvgl.h", "--output", "passport_hint_12.c",
], check=True, cwd=fonts)
