# Install MatrixRain.scr as the normal Windows screensaver.
# Rebuilds from source FIRST, so the installed copy always matches the
# current code - one script does the whole thing.
# Run from this folder. Needs admin (it writes to System32).
#   powershell -ExecutionPolicy Bypass -File install.ps1
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot\build.ps1"
$scr = "$PSScriptRoot\MatrixRain.scr"
if (-not (Test-Path $scr)) { throw "Build failed: $scr not found." }
Copy-Item $scr 'C:\Windows\System32\MatrixRain.scr' -Force

# The installed copy has no config.yaml next to it, so point it at the
# ONE config file in this folder (lookup order: MATRIXRAIN_CONFIG, then
# next to the .scr, then %APPDATA%\MatrixRain\config.yaml).
$config = "$PSScriptRoot\config.yaml"
[Environment]::SetEnvironmentVariable('MATRIXRAIN_CONFIG', $config, 'User')

$desk = 'HKCU:\Control Panel\Desktop'
Set-ItemProperty $desk 'SCRNSAVE.EXE' 'C:\Windows\System32\MatrixRain.scr'
Set-ItemProperty $desk 'ScreenSaveActive' '1'
Set-ItemProperty $desk 'ScreenSaveTimeOut' '600'
Set-ItemProperty $desk 'ScreenSaverIsSecure' '0'
rundll32.exe user32.dll,UpdatePerUserSystemParameters
"Installed: C:\Windows\System32\MatrixRain.scr"
"Timeout: 600 seconds (10 minutes), no lock on resume."
"Settings live in: $config (MATRIXRAIN_CONFIG; sign out and back in once so the idle timer sees it)"
