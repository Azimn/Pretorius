@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run_Character.ps1" -Character friendly -Renderer template
pause
