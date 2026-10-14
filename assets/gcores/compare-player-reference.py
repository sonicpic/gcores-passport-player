"""Compare supplied artwork with the actual LVGL reference fixture capture."""
from pathlib import Path
from PIL import Image, ImageDraw
root=Path(__file__).resolve().parent
actual=Image.open(root/'host-render/player-reference.ppm')
actual.save(root/'player-reference-lvgl.png')
reference=Image.open(root/'references/style-07-player.png').convert('RGB').resize((240,320),Image.Resampling.LANCZOS)
reference.save(root/'player-reference-scaled.png')
canvas=Image.new('RGB',(500,352),(12,14,14))
canvas.paste(reference,(5,25));canvas.paste(actual,(255,25))
draw=ImageDraw.Draw(canvas)
draw.text((5,5),'REFERENCE 240x320',fill='white')
draw.text((255,5),'ACTUAL LVGL',fill='white')
canvas.resize((1000,704),Image.Resampling.NEAREST).save(root/'player-reference-comparison.png')
print('Saved supplied-reference / actual native LVGL comparison')
