"""Render schematic SVG/PNG previews at firmware coordinates / 按固件坐标绘制示意图。"""
from pathlib import Path
from html import escape
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
FONT = HERE.parent / "fonts" / "NotoSansCJKsc-Regular.otf"
BG, PANEL, LINE, MUTED, WHITE, RED = "#0b0d11", "#15191f", "#282d36", "#89919d", "#f1f3f5", "#ff3d1d"

class Screen:
    def __init__(self):
        self.image = Image.new("RGB", (240, 320), BG)
        self.draw = ImageDraw.Draw(self.image)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="240" height="320" viewBox="0 0 240 320"><style>@font-face{{font-family:PreviewNoto;src:url("../fonts/NotoSansCJKsc-Regular.otf")}}text{{font-family:PreviewNoto,"Noto Sans CJK SC",sans-serif}}</style><rect width="240" height="320" fill="{BG}"/>']

    def box(self, x, y, w, h, color):
        self.draw.rectangle((x, y, x+w-1, y+h-1), fill=color)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{color}"/>')

    def text(self, x, y, content, size=14, color=WHITE):
        baseline = y + (17 if size == 16 else size)
        self.draw.text((x, baseline), content, font=ImageFont.truetype(str(FONT), size), fill=color, anchor="ls")
        self.svg.append(f'<text x="{x}" y="{baseline}" font-size="{size}" fill="{color}">{escape(content)}</text>')

    def circle(self, x, y, r, fill, stroke=None, width=1):
        self.draw.ellipse((x-r,y-r,x+r,y+r), fill=fill, outline=stroke, width=width)
        self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke or fill}" stroke-width="{width}"/>')

    def shell(self, heading, channel="机核电台 / AI PASSPORT", hint="UP/DOWN  OK  HOLD: MENU"):
        self.box(12,11,4,14,RED)
        self.text(23,9,"GCORES")
        self.text(137,10,"NET")
        self.text(199,10,"87%")
        self.box(12,32,216,1,LINE)
        self.text(12,42,heading,20)
        self.text(12,67,channel,16,MUTED)
        self.box(12,296,216,1,LINE)
        self.text(12,300,hint,14,MUTED)

    def rows(self, titles, selected=0):
        for i,title in enumerate(titles):
            y=99+i*34
            self.box(12,y,216,32,"#342023" if i==selected else PANEL)
            if i==selected: self.box(12,y,2,32,RED)
            self.text(20,y+9,f"{i+1:02}",14,RED if i==selected else MUTED)
            self.text(46,y+4,title,16)

    def save(self,name):
        (HERE/f"{name}.svg").write_text("".join(self.svg)+"</svg>\n",encoding="utf-8")
        return self.image

pages = []
s=Screen(); s.shell("RADIO LIBRARY",hint="OK: PLAY   HOLD: SYSTEM")
s.rows(["电子游戏里的声音","从像素到真实世界","那些改变我们的游戏","电台里的一封来信","游戏与生活的交点"])
s.text(12,269,"最近更新 / 01 - 05",16,MUTED); pages.append(("library",s.save("library")))

s=Screen(); s.shell("NOW PLAYING","机核游戏频道",hint="OK: PAUSE   HOLD: SYSTEM")
s.box(12,99,216,79,PANEL)
for x in range(0,216,18): s.box(12+x,99,1,79,LINE)
for y in range(0,79,18): s.box(12,99+y,216,1,LINE)
s.circle(122,138,30,BG,RED,2); s.circle(122,138,7,RED)
s.text(20,108,"GC / 01",14,MUTED); s.text(170,153,"MONO",14,MUTED)
s.text(12,183,"电子游戏里的声音",16); s.text(12,205,"与记忆",16)
s.text(12,236,"ON AIR",14,RED); s.text(176,236,"VOL 65",14,MUTED)
s.box(12,259,216,3,LINE); s.box(12,259,82,3,RED)
s.text(12,273,"24:18  /  63:40",14,MUTED); pages.append(("player",s.save("player")))

s=Screen(); s.shell("FREQUENCIES",hint="UP/DOWN   OK: SELECT")
s.rows(["机核游戏频道","机核生活频道","机核音频频道","机核 PRO","核市奇谭"],1)
s.text(12,269,"选择你的电台频率",16,MUTED); pages.append(("channels",s.save("channels")))

s=Screen(); s.shell("CHAPTER MARKS", "电子游戏里的声音", "UP/DOWN   OK: SEEK")
s.rows(["00:00 开场","06:20 第一段声音","21:10 记忆的回声","38:40 游戏与音乐","55:30 尾声"],2)
s.text(12,269,"章节 / 03 - 05",16,MUTED); pages.append(("chapters",s.save("chapters")))

s=Screen(); s.shell("SYSTEM",hint="UP/DOWN   OK: CHANGE")
s.rows(["更新当前栏目","Wi-Fi 配网","定时睡眠：关闭","立即息屏","保存并睡眠"],1)
s.text(12,269,"息屏后继续收听",16,MUTED); pages.append(("settings",s.save("settings")))

s=Screen(); s.shell("NETWORK", "本地配网 / WIFI SETUP", "HOLD OK: BACK")
for i,line in enumerate(["手机连接热点","Gcores-Passport","密码见设备屏幕","浏览器打开","http://192.168.4.1","使用 2.4 GHz 网络"]):
    s.text(12,99+i*25,line,16,WHITE if i in [1,4] else MUTED)
pages.append(("wifi",s.save("wifi")))

contact = Image.new("RGB",(6*264+24,382),"#07080b")
d=ImageDraw.Draw(contact)
for i,(name,im) in enumerate(pages):
    contact.paste(im,(24+i*264,36))
    d.text((24+i*264,12),name.upper(),font=ImageFont.truetype(str(FONT),12),fill=MUTED)
contact.resize((contact.width*2,contact.height*2),Image.Resampling.NEAREST).save(HERE/"preview.png")

cards="".join(f'<figure><figcaption>{name.upper()}</figcaption><img width="240" height="320" src="{name}.svg" alt="{name} design preview"/></figure>' for name,_ in pages)
(HERE/"preview.html").write_text(f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>GCORES / Passport UI</title><style>body{{background:#07080b;color:#f1f3f5;margin:36px;font:14px system-ui}}h1{{font-size:24px;letter-spacing:2px}}p,figcaption{{color:#89919d}}main{{display:flex;gap:24px;flex-wrap:wrap}}figure{{margin:12px 0}}figcaption{{margin-bottom:12px;letter-spacing:2px}}img{{border:1px solid #282d36}}a{{color:#ff3d1d}}</style><h1>GCORES / PASSPORT</h1><p>240 × 320 · 原创竖屏电台仪表 / Original portrait radio instrument</p><main>{cards}</main><p>布局坐标与固件一致。桌面字体渲染为近似值，非设备截图。<br>Coordinates match firmware. Desktop typography is approximate; these are not device screenshots.</p><p><a href="README.zh_CN.md">设计与素材说明 / Design &amp; source notes</a></p></html>''',encoding="utf-8")
print("Generated 6 SVGs, preview.html, preview.png; device rendering remains unverified.")
