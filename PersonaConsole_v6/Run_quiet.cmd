@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run_Character.ps1" -Character quiet -Renderer template
pause
