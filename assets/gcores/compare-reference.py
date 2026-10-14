"""Original reference vs actual LVGL at equal panel scale / 原图与实际LVGL等比例对照。"""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parent
pairs=[('style-02-player.png','player-lvgl.png','播放器'),
       ('style-01-library.png','channels-lvgl.png','栏目页'),
       ('style-03-menu.png','settings-lvgl.png','设置页（仅真实功能）'),
       ('style-04-timeline.png','timeline-lvgl.png','文本时间轴（阅读不定位）')]
canvas=Image.new('RGB',(1984,1424),'#070b0e')
draw=ImageDraw.Draw(canvas)
font=ImageFont.truetype(str(root.parent/'fonts/NotoSansCJKsc-Regular.otf'),20)
for i,(ref,actual,label) in enumerate(pairs):
    x=16+(i%2)*992;y=16+(i//2)*712
    draw.text((x,y),f'{label} · 用户原图 / 实际 LVGL',font=font,fill='#f1f3f5')
    source=Image.open(root/'references'/ref).convert('RGB').resize((480,640),Image.Resampling.BOX)
    page=Image.open(root/actual).convert('RGB').resize((480,640),Image.Resampling.NEAREST)
    canvas.paste(source,(x,y+40));canvas.paste(page,(x+496,y+40))
canvas.save(root/'reference-comparison.png')
print('Saved equal-scale original / actual native renderer comparison.')
