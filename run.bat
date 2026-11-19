@echo off
rem Matrix Rain screensaver — double-click to start the rain right away.
rem Runs the copy in this folder if it's here, otherwise the installed one.
if exist "%~dp0MatrixRain.scr" (
    start "" "%~dp0MatrixRain.scr" /s
) else (
    start "" "C:\Windows\System32\MatrixRain.scr" /s
)
