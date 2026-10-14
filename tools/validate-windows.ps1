param([ValidateSet('firmware')][string]$Mode = 'firmware')
$ErrorActionPreference = 'Stop'
# This is native PowerShell, even when dispatched by Git Bash. Inherited
# MSYSTEM markers make idf.py reject the shell before it detects Windows.
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYS2_PATH_TYPE -ErrorAction SilentlyContinue
$passportRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $passportRoot
try {
    $passportVersion = (& python "$env:IDF_PATH/tools/idf.py" --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $passportVersion -notmatch '^ESP-IDF v5\.5\.3(?:-dirty)?$') {
        throw 'Activate ESP-IDF 5.5.3 before running the firmware gate.'
    }
    # Keep an isolated configuration, exactly as the shared POSIX gate does.
    # Retain failed builds for diagnosis; no recursive cleanup of user paths.
    $passportBuild = Join-Path $passportRoot ('build/validation-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $passportBuild | Out-Null
    $env:SDKCONFIG_DEFAULTS = Join-Path $passportRoot 'sdkconfig.defaults'
    & python "$env:IDF_PATH/tools/idf.py" -B $passportBuild -D "SDKCONFIG=$passportBuild/sdkconfig" build
    if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
    & python "$env:IDF_PATH/tools/idf.py" -B $passportBuild merge-bin -o "$passportBuild/FoloToy-AI-Passport-full.bin"
    if ($LASTEXITCODE -ne 0) { throw 'Image merge failed.' }
    & python tools/verify_firmware.py $passportBuild
    if ($LASTEXITCODE -ne 0) { throw 'Firmware layout verification failed.' }
    & python tools/archive_firmware.py create $passportBuild --archive-root "$passportRoot/build/firmware"
    if ($LASTEXITCODE -ne 0) { throw 'Matching firmware/debug archive failed.' }
    Copy-Item -LiteralPath "$passportBuild/FoloToy-AI-Passport-full.bin" -Destination "$passportRoot/build/FoloToy-AI-Passport-full.bin"
    Write-Output 'Firmware build: PASS'
} finally { Pop-Location }
