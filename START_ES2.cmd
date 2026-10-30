@echo off
setlocal DisableDelayedExpansion
rem System PowerShell 7 (Microsoft Store app alias, else pwsh.exe on PATH); do not change execution policies.
set "ES2_POWERSHELL=%LOCALAPPDATA%\Microsoft\WindowsApps\pwsh.exe"
if not exist "%ES2_POWERSHELL%" for /f "delims=" %%P in ('where pwsh.exe 2^>nul') do set "ES2_POWERSHELL=%%P"
set "ES2_START_SCRIPT=%~dp0tools\reference-game\setup\start-native.ps1"

if not exist "%ES2_POWERSHELL%" goto missing_powershell
if not exist "%ES2_START_SCRIPT%" goto missing_script

echo EarthSiege II wird vorbereitet...
"%ES2_POWERSHELL%" -NoLogo -NoProfile -File "%ES2_START_SCRIPT%" %*
set "ES2_EXIT_CODE=%ERRORLEVEL%"
if not "%ES2_EXIT_CODE%"=="0" goto failed
exit /b 0

:missing_powershell
echo PowerShell 7 (pwsh.exe) wurde nicht gefunden:
echo "%ES2_POWERSHELL%"
echo Bitte PowerShell 7 installieren oder den Startpfad pruefen lassen.
set "ES2_EXIT_CODE=1"
goto failed

:missing_script
echo Das Startskript wurde nicht gefunden:
echo "%ES2_START_SCRIPT%"
echo START_ES2.cmd muss im es2-Projektordner bleiben.
set "ES2_EXIT_CODE=1"

:failed
echo.
echo EarthSiege II konnte nicht gestartet werden. Fehler siehe oben.
pause
exit /b %ES2_EXIT_CODE%
