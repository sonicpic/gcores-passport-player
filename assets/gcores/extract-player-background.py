"""Extract the latest supplied player plate, clearing only live data fields."""
from pathlib import Path
import hashlib
from PIL import Image

root = Path(__file__).resolve().parent
source = root / 'references/style-07-player.png'
im = Image.open(source).convert('RGB')
original = im.copy()
pixels = im.load()


def clear_rect(x1, y1, x2, y2, left, right):
    """Interpolate untouched adjacent source background, retaining original art."""
    for y in range(y1, y2):
        a = tuple(sum(pixels[x, y][c] for x in range(left, left + 5)) // 5 for c in range(3))
        b = tuple(sum(pixels[x, y][c] for x in range(right, right + 5)) // 5 for c in range(3))
        for x in range(x1, x2):
            f = (x - x1) / max(1, x2 - x1 - 1)
            pixels[x, y] = tuple(round(a[c] * (1 - f) + b[c] * f) for c in range(3))


# Original header, cover, timeline dots/axis, character and footer stay exact.
# Only actual title/episode/channel/chapters and status values are mutable.
clear_rect(45, 153, 1050, 290, 25, 1058)
clear_rect(455, 326, 1050, 385, 435, 1058)
clear_rect(528, 432, 1050, 508, 516, 1058)
clear_rect(528, 571, 1050, 622, 516, 1058)
clear_rect(698, 34, 857, 89, 692, 861)
clear_rect(899, 49, 944, 72, 879, 962)
clear_rect(973, 35, 1050, 89, 965, 1058)
player = im.resize((240, 320), Image.Resampling.LANCZOS)
# Latest two supplied references update only header and browser/settings footer.
# The style-07 player body and player footer remain unchanged.
home_source = root / 'references/style-12-home-footer.png'
settings_source = root / 'references/style-11-settings-footer.png'
home_original = Image.open(home_source).convert('RGB')
settings_original = Image.open(settings_source).convert('RGB')
# Footer revisions must not change the accepted shared header.
header_source = Image.open(root / 'references/style-08-home-footer.png').convert('RGB')
saved_pixels = pixels
pixels = header_source.load()
clear_rect(739, 34, 846, 89, 730, 850)  # mutable volume bars
clear_rect(861, 35, 916, 89, 850, 920)  # mutable volume number
clear_rect(953, 40, 1004, 79, 932, 1019)  # battery interior; retain outline
clear_rect(1020, 35, 1066, 88, 1019, 1068)  # battery numeric state
pixels = saved_pixels
header = header_source.resize((240, 320), Image.Resampling.LANCZOS).crop((0, 0, 240, 26))
player.paste(header, (0, 0))
home_footer = home_original.resize((240, 320), Image.Resampling.LANCZOS).crop((0, 286, 240, 320))
settings_footer = settings_original.resize((240, 320), Image.Resampling.LANCZOS).crop((0, 286, 240, 320))

# Browser pages use a real pure-black fill and only retain the exact header.
# A 240x26 Flash plate saves 141,120 bytes versus the former full-screen image.
common = header.copy()

out = root / 'extracted'
out.mkdir(exist_ok=True)
assets = {
    'passport_player_background': player,
    'passport_common_background': common,
    'passport_home_footer': home_footer,
    'passport_settings_footer': settings_footer,
    'passport_header_speaker': original.crop((640, 33, 692, 85)).resize((12, 12), Image.Resampling.LANCZOS),
    'passport_footer_arrows': original.crop((53, 1322, 187, 1370)).resize((29, 11), Image.Resampling.LANCZOS),
    'passport_footer_ok': original.crop((563, 1307, 640, 1386)).resize((18, 18), Image.Resampling.LANCZOS),
}
parts = ['/* Latest original user plate; clear live data only; Flash RGB565. */', '#include "lvgl.h"']
for name, asset in assets.items():
    asset.save(out / (name + '.png'))
    data = bytearray()
    for r, g, b in asset.getdata():
        p = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        data.extend((p & 255, p >> 8))
    rows = [','.join(f'0x{v:02x}' for v in data[i:i + 32]) for i in range(0, len(data), 32)]
    w, h = asset.size
    parts += [f'static const uint8_t {name}_data[] = {{\n' + ',\n'.join(rows) + '\n};',
              f'const lv_image_dsc_t {name} = {{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w={w},.h={h},.stride={w * 2}}},.data_size=sizeof({name}_data),.data={name}_data}};']
(out / 'passport_player_background.c').write_text('\n'.join(parts) + '\n', encoding='utf-8')
print('Source SHA256', hashlib.sha256(source.read_bytes()).hexdigest())
print('Home footer SHA256', hashlib.sha256(home_source.read_bytes()).hexdigest())
print('Settings footer SHA256', hashlib.sha256(settings_source.read_bytes()).hexdigest())
print('Flash bitmap bytes', sum(a.width * a.height * 2 for a in assets.values()))
