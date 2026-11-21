# Install MatrixRain.scr as the normal Windows screensaver.
# Rebuilds from source FIRST, so the installed copy always matches the
# current code - one script does the whole thing.
# Needs admin (it writes to System32). From the repo folder:
#   powershell -ExecutionPolicy Bypass -File scripts\windows\install.ps1
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot\build.ps1"
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$scr = "$root\MatrixRain.scr"
if (-not (Test-Path $scr)) { throw "Build failed: $scr not found." }
Copy-Item $scr 'C:\Windows\System32\MatrixRain.scr' -Force

# The installed copy has no config.yaml next to it, so point it at the
# ONE config file in the repo folder (lookup order: MATRIXRAIN_CONFIG, then
# next to the .scr, then %APPDATA%\MatrixRain\config.yaml).
$config = "$root\config.yaml"
[Environment]::SetEnvironmentVariable('MATRIXRAIN_CONFIG', $config, 'User')
# Programs already running (and the idle timer) keep the old environment
# until you sign out, so also make the per-user config a link to the repo's
# (a real file already there is kept as config.yaml.bak). Needs admin,
# which this script has.
$userCfg = Join-Path $env:APPDATA 'MatrixRain\config.yaml'
New-Item -ItemType Directory -Force (Split-Path $userCfg) | Out-Null
$item = Get-Item $userCfg -Force -ErrorAction SilentlyContinue
if ($item -and -not $item.LinkType) { Move-Item $userCfg "$userCfg.bak" -Force }
elseif ($item) { Remove-Item $userCfg -Force }
New-Item -ItemType SymbolicLink -Path $userCfg -Target $config | Out-Null
# Bundled open fonts (fonts\open\) and vendored ones (fonts\vendor\,
# git-ignored), copied beside the per-user
# config too, so they still load if MATRIXRAIN_CONFIG is unset.
$fontDir = Join-Path $env:APPDATA 'MatrixRain\fonts'
New-Item -ItemType Directory -Force $fontDir | Out-Null
Get-ChildItem (Join-Path $root 'fonts\vendor'),(Join-Path $root 'fonts\open') -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -match '^\.(ttf|ttc|otf)$' } |
    ForEach-Object { Copy-Item $_.FullName $fontDir -Force; "Font: $($_.Name)" }

$desk = 'HKCU:\Control Panel\Desktop'
Set-ItemProperty $desk 'SCRNSAVE.EXE' 'C:\Windows\System32\MatrixRain.scr'
Set-ItemProperty $desk 'ScreenSaveActive' '1'
Set-ItemProperty $desk 'ScreenSaveTimeOut' '600'
Set-ItemProperty $desk 'ScreenSaverIsSecure' '0'
rundll32.exe user32.dll,UpdatePerUserSystemParameters
"Installed: C:\Windows\System32\MatrixRain.scr"
"Timeout: 600 seconds (10 minutes), no lock on resume."
"Settings live in: $config (MATRIXRAIN_CONFIG; sign out and back in once so the idle timer sees it)"
