"""Audit real generated glyphs, metrics and Flash budget / 核对字形、度量与 Flash。"""
from pathlib import Path
import re
import json
import hashlib

fonts = Path(__file__).resolve().parent
root = fonts.parents[1]
ascii_inventory = set(range(0x20, 0x7F))
data = fonts / "standard-characters"
manifest = json.loads((data / "manifest.json").read_text(encoding="utf-8"))
for filename, expected in manifest["sha256"].items():
    assert hashlib.sha256((data / filename).read_bytes()).hexdigest() == expected, filename
levels = ["".join((data / f"level-{i}.txt").read_text(encoding="utf-8").split()) for i in (1, 2, 3)]
for text, count in zip(levels, (3500, 3000, 1605)):
    assert len(text) == len(set(text)) == count
standard = "".join(levels)
assert len(standard) == len(set(standard)) == 8105
assert standard == (data / "characters.txt").read_text(encoding="utf-8").strip()
standard_inventory = set(map(ord, standard))
non_chinese = {int(line[2:], 16) for line in (data / "non-chinese-codepoints.txt").read_text().splitlines()}
assert len(non_chinese) == 512
cjk_required = standard_inventory | non_chinese
assert len(standard_inventory - set(range(0x4E00, 0x9FF0))) == 273



def inspect(name, max_height):
    source = (fonts / f"{name}.c").read_text(encoding="utf-8")
    inventory = {int(x, 16) for x in re.findall(r"/\* U\+([A-F0-9]+) ", source)}
    bounds = [tuple(map(int, m)) for m in re.findall(
        r"\.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)", source)]
    line = int(re.search(r"\.line_height = (\d+)", source).group(1))
    base = int(re.search(r"\.base_line = (\d+)", source).group(1))
    assert len(bounds) == len(inventory) + 1, f"{name}: descriptor inventory mismatch"
    assert max(d[1] + d[3] for d in bounds) <= line - base, f"{name}: top clipped"
    assert min(d[3] for d in bounds) >= -base, f"{name}: bottom clipped"
    assert line <= max_height, f"{name}: unexpected line height {line}"
    assert re.search(r"\.bpp = 2\b", source), f"{name}: expected antialiased 2 bpp"
    assert re.search(r"\.bitmap_format = 0\b", source), f"{name}: expected plain bitmap"
    assert re.search(rf"const lv_font_t {name}\b", source), f"{name}: mismatched symbol"
    bitmap = len(re.findall(r"0x[0-9a-f]+", source[:source.find("GLYPH DESCRIPTION")]))
    # ESP32 large-format descriptors are exactly 16 bytes; cmaps/font metadata
    # add a small target-dependent amount checked by the actual firmware gate.
    estimate = bitmap + len(bounds) * 16
    print(f"PASS: {name}: {len(inventory):,} glyphs, line {line}, baseline {base}, "
          f"bitmap {bitmap:,} B, bitmap+descriptors {estimate:,} B")
    return inventory, estimate


complete = []
for name, height in (("passport_cjk_16", 24), ("passport_cjk_bold_22", 32),
                     ("passport_cjk_12", 17)):
    inventory, estimate = inspect(name, height)
    missing = sorted(cjk_required - inventory)
    assert not missing, f"{name}: missing " + ", ".join(f"U+{x:04X}" for x in missing[:20])
    assert inventory == cjk_required, f"{name}: extras {inventory - cjk_required}"
    assert not {0x1F600, 0x9FF0, 0x9F98} & inventory, f"{name}: negative boundary probe"
    complete.append((inventory, estimate))
assert sum(size for _, size in complete) < 2_400_000, "Complete fonts exceeded Flash budget"

for size, height in ((12, 14), (14, 17)):
    inventory, _ = inspect(f"passport_sans_{size}", height)
    assert inventory == ascii_inventory, f"Sans {size}: incomplete ASCII"

catalog = (root / "main/passport_catalog.cpp").read_text(encoding="utf-8")
channels = catalog.split("kChannels[] = {", 1)[1].split("};", 1)[0]
channel_inventory, _ = inspect("passport_channel_11", 16)
channel_required = {ord(c) for c in channels if ord(c) >= 128} | ascii_inventory
channel_required |= {ord(c) for c in "机核电台"}
assert not channel_required - channel_inventory, "Fixed channel font misses catalog labels"

hint_inventory, _ = inspect("passport_hint_12", 16)
main = (root / "main/main.c").read_text(encoding="utf-8")
hint_lines = "".join(line for line in main.splitlines() if re.search(r"\bhint\s*=", line))
hint_required = {ord(c) for c in hint_lines + "音量进度播放暂停菜单选择栏目打开设置调整返回确认浏览" if ord(c) >= 128} | ascii_inventory
assert not hint_required - hint_inventory, "Fixed button-hint font misses actual UI text"
print("PASS: exactly 8,105 standard Chinese characters plus unchanged 512 non-Chinese glyphs at every dynamic size; unsupported boundaries rejected")
