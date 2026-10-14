# Convert the licensed Monocraft source into renamed LVGL pixel supplements.
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $pixelConverter = 'font-tools/node_modules/lv_font_conv/lv_font_conv.js'
    if (-not (Test-Path -LiteralPath $pixelConverter)) { throw 'Install the pinned font-tools dependencies first.' }
    foreach ($pixelSize in @(14,20)) {
        $pixelName = "passport_pixel_$pixelSize"
        & node $pixelConverter --font Monocraft.ttf --range 0x20-0x7E --size $pixelSize --bpp 1 --format lvgl --no-compress --no-kerning --lv-font-name $pixelName --lv-include lvgl.h --output "$pixelName.c"
        if ($LASTEXITCODE -ne 0) { throw 'Pixel font conversion failed.' }
        $pixelPath = Join-Path $PSScriptRoot "$pixelName.c"
        $pixelLicense = "/* SPDX-License-Identifier: OFL-1.1`n * Renamed bitmap derivative of Monocraft; see Monocraft-OFL.txt and README.md.`n */`n"
        [System.IO.File]::WriteAllText($pixelPath, $pixelLicense + [System.IO.File]::ReadAllText($pixelPath), [System.Text.UTF8Encoding]::new($false))
    }
    & python verify-font.py
    if ($LASTEXITCODE -ne 0) { throw 'Font coverage audit failed.' }
} finally { Pop-Location }
