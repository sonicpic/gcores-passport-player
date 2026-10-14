"""Compare licensed source weights at device resolution / 对比设备字号的真实字重。"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

fonts = Path(__file__).resolve().parent
preview = Image.new("RGB", (720, 864), "#161616")
for row, weight in enumerate(("Regular", "Bold", "Black")):
    sample = Image.new("RGB", (240, 96), "#161616")
    draw = ImageDraw.Draw(sample)
    source = fonts / f"NotoSansCJKsc-{weight}.otf"
    draw.text((8, 2), f"{weight} 22 px",
              font=ImageFont.truetype(str(fonts / "NotoSansCJKsc-Regular.otf"), 12),
              fill="#ff5d2c")
    draw.text((8, 25), "游戏，只是开始", font=ImageFont.truetype(str(source), 22),
              fill="#fff5e5")
    draw.text((8, 57), "播客列表 设置", font=ImageFont.truetype(str(source), 16),
              fill="#fff5e5")
    preview.paste(sample.resize((720, 288), Image.Resampling.NEAREST), (0, row * 288))
preview.save(fonts / "font-weight-comparison.png")
