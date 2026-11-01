@echo off
setlocal
set "vrPowerShell=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "vrPowerShell=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%vrPowerShell%" (
  echo Windows PowerShell was not found. Nothing was launched.
  pause
  exit /b 1
)
"%vrPowerShell%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Play-HarleyVR.ps1" -Runtime SteamVR %*
set "vrResult=%ERRORLEVEL%"
if not "%vrResult%"=="0" pause
exit /b %vrResult%
