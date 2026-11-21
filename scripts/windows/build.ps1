# Build MatrixRain.scr from the C sources (engine\ + platform\windows\).
# Right-click > Run with PowerShell, or from the repo folder:
#   powershell -ExecutionPolicy Bypass -File scripts\windows\build.ps1          # build
#   powershell -ExecutionPolicy Bypass -File scripts\windows\build.ps1 -Test    # build + run engine tests
# Writes MatrixRain.scr to the repo folder.
#
# Uses the first compiler it finds:
#   1. Visual Studio / Build Tools (MSVC)
#   2. MinGW-w64 gcc on PATH
#   3. MinGW-w64 cross gcc inside WSL (sudo apt install gcc-mingw-w64-x86-64 make)
param([switch]$Test)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = Join-Path $root 'MatrixRain.scr'
$testExe = Join-Path $root 'build\engine_test.exe'
New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
$engine = @('engine\config.c', 'engine\paths.c', 'engine\rain.c', 'engine\render.c')
$libs = 'user32.lib gdi32.lib comctl32.lib shell32.lib'

Push-Location $root
try {
    $vs = $null
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    $wslCross = $false
    if (-not $vs -and -not $gcc -and (Get-Command wsl -ErrorAction SilentlyContinue)) {
        wsl -e sh -c 'command -v x86_64-w64-mingw32-gcc >/dev/null && command -v make >/dev/null' 2>$null
        $wslCross = ($LASTEXITCODE -eq 0)
    }

    if ($vs) {
        "Compiler: MSVC ($vs)"
        $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
        $cflags = '/nologo /O2 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS /Fobuild\'
        $cmds = @(
            'rc /nologo /fo build\matrixrain.res platform\windows\matrixrain.rc',
            "cl $cflags $($engine -join ' ') platform\windows\main.c build\matrixrain.res /Fe:MatrixRain.scr /link /SUBSYSTEM:WINDOWS $libs"
        )
        if ($Test) { $cmds += "cl $cflags $($engine -join ' ') tests\engine_test.c /Fe:build\engine_test.exe" }
        cmd /c "call `"$vcvars`" >nul && $($cmds -join ' && ')"
        if ($LASTEXITCODE -ne 0) { throw "MSVC build failed (exit $LASTEXITCODE)" }
    }
    elseif ($gcc) {
        "Compiler: gcc ($($gcc.Source))"
        & windres platform/windows/matrixrain.rc -O coff -o build/matrixrain.res.o
        if ($LASTEXITCODE -ne 0) { throw 'windres failed' }
        & gcc -O2 -Wall -mwindows -o MatrixRain.scr @engine platform/windows/main.c build/matrixrain.res.o -lcomctl32 -lgdi32 -luser32 -lshell32
        if ($LASTEXITCODE -ne 0) { throw 'gcc failed' }
        if ($Test) {
            & gcc -O2 -Wall -o build/engine_test.exe @engine tests/engine_test.c
            if ($LASTEXITCODE -ne 0) { throw 'gcc (tests) failed' }
        }
    }
    elseif ($wslCross) {
        'Compiler: MinGW-w64 cross gcc in WSL'
        $target = if ($Test) { 'windows windows-test' } else { 'windows' }
        wsl --cd "$root" -e sh -c "make $target"
        if ($LASTEXITCODE -ne 0) { throw "WSL cross build failed (exit $LASTEXITCODE)" }
    }
    else {
        throw @"
No C compiler found. Install one of:
  - Visual Studio Build Tools:  winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  - MinGW-w64 (WinLibs):        winget install BrechtSanders.WinLibs.POSIX.UCRT
  - or inside WSL:              sudo apt install gcc-mingw-w64-x86-64 make
"@
    }

    $f = Get-Item $out
    "Built: $($f.FullName) ($($f.Length) bytes)"
    if ($Test) {
        & $testExe
        if ($LASTEXITCODE -ne 0) { throw "engine tests failed (exit $LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}
