"""Extract user artwork onto transparent, equally sized Flash icon plates."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parent
out = root / 'extracted'
parts = ['/* Original user artwork, RGB565 + A8, immutable Flash data. */', '#include "lvgl.h"']
for name, source in [('passport_return_icon', 'settings-return-source.png'),
                     ('passport_fm_icon', 'settings-fm-source.png')]:
    image = Image.open(root / 'references' / source).convert('RGBA')
    # The supplied background is dark; preserve the bright white/orange artwork
    # and edge antialiasing without retaining the unrelated rounded dark tile.
    px = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, _ = px[x, y]
            a = max(0, min(255, (max(r, g, b) - 35) * 255 // 130))
            px[x, y] = (r, g, b, a)
    bounds = image.getchannel('A').getbbox()
    crop = image.crop(bounds)
    crop.thumbnail((24, 24), Image.Resampling.LANCZOS)
    icon = Image.new('RGBA', (24, 24))
    icon.paste(crop, ((24-crop.width)//2, (24-crop.height)//2))
    icon.save(out / f'{name}.png', optimize=True)
    color, alpha = bytearray(), bytearray()
    for r, g, b, a in icon.getdata():
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        color.extend((value & 255, value >> 8)); alpha.append(a)
    data = color + alpha
    rows = [','.join(f'0x{v:02x}' for v in data[i:i+32]) for i in range(0, len(data), 32)]
    parts += [f'static const uint8_t {name}_data[] = {{\n' + ',\n'.join(rows) + '\n};',
              f'const lv_image_dsc_t {name} = {{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565A8,.w=24,.h=24,.stride=48}},.data_size=sizeof({name}_data),.data={name}_data}};']
(out / 'passport_settings_icons.c').write_text('\n'.join(parts) + '\n', encoding='utf-8')
print('Flash icon data: 3456 bytes; no whole-image decompression buffer')
