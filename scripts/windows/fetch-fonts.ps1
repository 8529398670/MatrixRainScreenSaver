# Copy fonts you are licensed to use into fonts\vendor\ (git-ignored), so the
# saver can load them on machines where they aren't installed. Windows
# already has MS Gothic; this is for carrying it to your other computers
# (copy the folder over, or run scripts/fetch-fonts.sh there).
#   powershell -ExecutionPolicy Bypass -File scripts\windows\fetch-fonts.ps1            # MS Gothic
#   powershell -ExecutionPolicy Bypass -File scripts\windows\fetch-fonts.ps1 a.ttf b.otf  # these files
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$dest = Join-Path $root 'fonts\vendor'
New-Item -ItemType Directory -Force $dest | Out-Null
$files = if ($args.Count) { $args } else { @("$env:WINDIR\Fonts\msgothic.ttc") }
foreach ($f in $files) {
    if (-not (Test-Path $f)) { throw "Not found: $f" }
    Copy-Item $f $dest -Force
    "Vendored: $dest\$(Split-Path $f -Leaf)"
}
