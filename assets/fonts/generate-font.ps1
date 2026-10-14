# Generate the licensed dynamic-title font / 生成动态中文标题字体。
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    npm install --prefix font-tools --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'Font converter installation failed' }
    node font-tools/node_modules/lv_font_conv/lv_font_conv.js --font NotoSansCJKsc-Bold.otf --range 0x20-0x7E,0xA0-0xFF,0x2000-0x206F,0x3000-0x3030,0x3033-0x303F,0x4E00-0x9FFF,0xFF00-0xFFEF --size 16 --bpp 2 --format lvgl --no-compress --no-kerning --lv-font-name passport_cjk_16 --lv-include lvgl.h --output passport_cjk_16.c
    if ($LASTEXITCODE -ne 0) { throw 'Font conversion failed' }
    node font-tools/node_modules/lv_font_conv/lv_font_conv.js --font NotoSansCJKsc-Black.otf --range 0x20-0x7E,0xA0-0xFF,0x2000-0x206F,0x3000-0x3030,0x3033-0x303F,0x4E00-0x9FFF,0xFF00-0xFFEF --size 22 --bpp 2 --format lvgl --no-compress --no-kerning --lv-font-name passport_cjk_bold_22 --lv-include lvgl.h --output passport_cjk_bold_22.c
    if ($LASTEXITCODE -ne 0) { throw 'Heading font conversion failed' }
    node font-tools/node_modules/lv_font_conv/lv_font_conv.js --font NotoSansCJKsc-Regular.otf --range 0x20-0x7E,0xA0-0xFF,0x2000-0x206F,0x3000-0x3030,0x3033-0x303F,0x4E00-0x9FFF,0xFF00-0xFFEF --size 12 --bpp 2 --format lvgl --no-compress --no-kerning --lv-font-name passport_cjk_12 --lv-include lvgl.h --output passport_cjk_12.c
    if ($LASTEXITCODE -ne 0) { throw 'Chapter font conversion failed' }
    foreach ($size in @(12, 14)) {
        node font-tools/node_modules/lv_font_conv/lv_font_conv.js --font NotoSansCJKsc-Regular.otf --range 0x20-0x7E --size $size --bpp 2 --format lvgl --no-compress --no-kerning --lv-font-name "passport_sans_$size" --lv-include lvgl.h --output "passport_sans_$size.c"
        if ($LASTEXITCODE -ne 0) { throw 'Latin font conversion failed' }
    }
    python generate-channel-font.py
    if ($LASTEXITCODE -ne 0) { throw 'Channel font conversion failed' }
    python generate-hint-font.py
    if ($LASTEXITCODE -ne 0) { throw 'Hint font conversion failed' }
    python verify-font.py
    if ($LASTEXITCODE -ne 0) { throw 'Font coverage failed' }
} finally { Pop-Location }
