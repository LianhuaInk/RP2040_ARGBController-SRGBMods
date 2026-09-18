param([string]$ArduinoCli = 'arduino-cli')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$sketchPath = Join-Path $repoRoot 'firmware/Pico_SRGB_CDC_v2_1'
$buildPath = Join-Path $repoRoot 'build'
& $ArduinoCli compile --fqbn 'rp2040:rp2040:rpipico:usbstack=tinyusb,opt=Optimize2' --build-path $buildPath $sketchPath
if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed.' }
Write-Output ('Compiled UF2: ' + (Join-Path $buildPath 'Pico_SRGB_CDC_v2_1.ino.uf2'))
