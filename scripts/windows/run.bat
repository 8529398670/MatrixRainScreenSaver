@echo off
rem Matrix Rain screensaver — double-click to start the rain right away.
rem Runs the copy built in the repo folder if it's there, otherwise the installed one.
if exist "%~dp0..\..\MatrixRain.scr" (
    start "" "%~dp0..\..\MatrixRain.scr" /s
) else (
    start "" "C:\Windows\System32\MatrixRain.scr" /s
)
