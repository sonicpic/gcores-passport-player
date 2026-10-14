"""Combine actual native LVGL RGB565 captures / 合成真实原生 LVGL 捕获图。"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
root = Path(__file__).resolve().parent
names = ["library", "player", "channels", "chapters", "settings", "timeline", "library-2", "player-seek", "wifi", "help", "settings-2", "timeline-text"]
contact = Image.new("RGB", (1608, 740), "#07080b")
draw = ImageDraw.Draw(contact)
font = ImageFont.truetype(str(root.parent / "fonts/NotoSansCJKsc-Regular.otf"), 12)
for index, name in enumerate(names):
    page = Image.open(root / "host-render" / f"{name}.ppm")
    page.save(root / f"{name}-lvgl.png")
    column=index%6; row=index//6
    contact.paste(page, (24 + column * 264, 36 + row * 358))
    draw.text((24 + column * 264, 12 + row * 358), name.upper(), font=font, fill="#89919d")
contact.resize((3216, 1480), Image.Resampling.NEAREST).save(root / "preview-lvgl.png")
print("Saved actual native LVGL preview; no device rendering is claimed.")
