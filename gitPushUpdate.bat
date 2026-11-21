@echo off
cd /d "%~dp0"
if not exist .git git init -b main
git config user.name "8529398670"
git config user.email "banjo.kazooie.2955@gmail.com"

rem Next number = last commit message + 1, or 1 if it isn't a number
rem (or there is no commit yet).
set LAST=
for /f "delims=" %%i in ('git log -1 --pretty^=%%B 2^>nul') do set LAST=%%i
set NEXT=1
if "%LAST%"=="" goto :havename
echo %LAST%| findstr /r "^[0-9][0-9]*$" >nul
if not errorlevel 1 set /a NEXT=LAST+1
:havename

git add -A
git diff --cached --quiet
if %errorlevel%==0 (
  echo No changes to commit.
  goto :push
)

rem Drop the old local tags first, like the Linux script does.
for /f %%t in ('git tag -l') do git tag -d %%t >nul

if not "%~1"=="" goto :named
git commit -q -m "%NEXT%"
git tag v1.0.%NEXT%
echo Committed "%NEXT%" and tagged v1.0.%NEXT%.
goto :push

:named
git commit -q -m "%~1"
git tag v1.0.%~1
echo Committed "%~1" and tagged v1.0.%~1.

:push
rem Point origin at the MatrixRain repo (no-op if it already points there).
git remote add origin git@github-8529398670:8529398670/MatrixRainScreenSaver.git 2>nul || git remote set-url origin git@github-8529398670:8529398670/MatrixRainScreenSaver.git

git push origin --tags --force
git push origin main
if %errorlevel%==0 (
  echo GitHub is up to date.
) else (
  echo PUSH FAILED - see the error above.
)
pause