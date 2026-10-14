# Generate the licensed dynamic-title font / 生成动态中文标题字体。
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    npm install --prefix font-tools --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'Font converter installation failed' }
    node generate-standard-fonts.js
    if ($LASTEXITCODE -ne 0) { throw 'Standard-character font conversion failed' }
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
