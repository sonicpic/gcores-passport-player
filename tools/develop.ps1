param([ValidateSet('activate','static','firmware','all','build')][string]$Action='activate', [string]$IdfRoot, [string]$ToolsRoot)
$ErrorActionPreference='Stop'
$passportProject=Split-Path -Parent $PSScriptRoot
$passportWorkspace=Split-Path -Parent $passportProject
if (!$IdfRoot) { $IdfRoot=Join-Path $passportWorkspace '.tools/esp-idf-v5.5.3' }
if (!$ToolsRoot) { $ToolsRoot=Join-Path $passportWorkspace '.tools/espressif' }
if (!(Test-Path -LiteralPath "$IdfRoot/export.ps1")) { throw 'ESP-IDF 5.5.3 not installed; pass -IdfRoot and -ToolsRoot for another installation.' }
$env:IDF_TOOLS_PATH=$ToolsRoot
. "$IdfRoot/export.ps1"
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF activation failed.' }
Push-Location -LiteralPath $passportProject
try {
    if ($Action -eq 'activate') { & python "$env:IDF_PATH/tools/idf.py" --version }
    elseif ($Action -eq 'build') { & python "$env:IDF_PATH/tools/idf.py" -B build/native -D SDKCONFIG=build/native/sdkconfig build }
    else {
        $passportBash=Join-Path ${env:ProgramFiles} 'Git/bin/bash.exe'
        if (!(Test-Path -LiteralPath $passportBash)) { throw 'Git for Windows Bash is required for the shared host gate.' }
        $env:PYTHON=(Get-Command python).Source.Replace([char]92,[char]47)
        & $passportBash tools/validate.sh "--$Action"
    }
    if ($LASTEXITCODE -ne 0) { throw "Development command failed ($LASTEXITCODE)." }
} finally { Pop-Location }
