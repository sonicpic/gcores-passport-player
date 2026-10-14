"""Deterministic original-source crops; no redraw / 原图确定性裁切，不重绘。"""
from pathlib import Path
import hashlib
from PIL import Image

root = Path(__file__).resolve().parent
source = root / 'references/style-02-player.png'
im = Image.open(source).convert('RGB')
# Coordinates in the original 1086 x 1448 reference, not AI-generated graphics.
specs = [('passport_reference_cover', (73, 287, 407, 681), (82, 105)),
         ('passport_reference_robot', (689, 629, 1034, 881), (74, 54)),
         ('passport_reference_wave', (299, 750, 700, 881), (86, 28)),
         ('passport_reference_badge', (73, 287, 407, 681), (28, 28))]
out = root / 'extracted'
out.mkdir(exist_ok=True)
parts = ['/* User-supplied image crops; Flash-resident RGB565 / 用户原图裁切，Flash RGB565。 */', '#include "lvgl.h"']
for name, bounds, size in specs:
    crop = (Image.open(root / "references/style-11-list-icon.png").convert("RGB").crop((125,128,1128,1131))
            if name == "passport_reference_badge" else im.crop(bounds))
    crop.save(out / f'{name}-source.png')
    small = crop.resize(size, Image.Resampling.LANCZOS) if name == "passport_reference_badge" else crop.resize(size, Image.Resampling.BOX)
    small.save(out / f'{name}.png')
    data = bytearray()
    for r, g, b in small.getdata():
        pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        data.extend((pixel & 255, pixel >> 8))
    rows = [','.join(f'0x{v:02x}' for v in data[i:i+32]) for i in range(0,len(data),32)]
    parts += [f'static const uint8_t {name}_data[] = {{\n' + ',\n'.join(rows) + '\n};',
              f'const lv_image_dsc_t {name} = {{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w={size[0]},.h={size[1]},.stride={size[0]*2}}},.data_size=sizeof({name}_data),.data={name}_data}};']
(out / 'passport_reference_assets.c').write_text('\n'.join(parts)+'\n',encoding='utf-8')
print('Reference SHA256',hashlib.sha256(source.read_bytes()).hexdigest())
print('RGB565 bytes',sum(w*h*2 for _,_,(w,h) in specs))
